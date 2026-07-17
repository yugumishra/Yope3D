#include "UIManager.h"
#include "gpu/GpuDevice.h"
#include <algorithm>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Init / cleanup
// ---------------------------------------------------------------------------

void UIManager::init(GpuDevice& gpu, VkCommandPool commandPool,
                     VkDescriptorSetLayout textureLayout,
                     float /*screenW*/, float /*screenH*/)
{
    gpu_           = &gpu;
    commandPool_   = commandPool;
    textureLayout_ = textureLayout;

    createDescriptorPool(gpu.device());
    createDummyTexture();
}

void UIManager::createDescriptorPool(VkDevice device) {
    // Reserve slots for up to 64 UI textures (atlases + dummy).
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 64;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = 1;
    ci.pPoolSizes    = &poolSize;
    ci.maxSets       = 64;
    if (vkCreateDescriptorPool(device, &ci, nullptr, &uiDescPool_) != VK_SUCCESS)
        throw std::runtime_error("UIManager: failed to create descriptor pool");
}

void UIManager::createDummyTexture() {
    uint8_t white[4] = {255, 255, 255, 255};
    dummyTexture_ = std::make_unique<Texture>(
        Texture::load(*gpu_, commandPool_, textureLayout_, uiDescPool_, white, 1, 1)
    );
}

void UIManager::cleanup(VkDevice device) {
    for (auto& a : atlases_) a->destroy(device);
    atlases_.clear();
    labels_.clear();
    if (dummyTexture_) { dummyTexture_->destroy(device); dummyTexture_.reset(); }
    if (uiDescPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, uiDescPool_, nullptr);
        uiDescPool_ = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------

void UIManager::update(float dt) {
    for (auto& l : labels_) l->update(dt);
}

void UIManager::buildFrame(UIBuffer& buf, float screenW, float screenH) {
    buf.begin();
    drawCalls_.clear();

    // Depth-sort: lower depth renders first (drawn under higher depth).
    std::stable_sort(labels_.begin(), labels_.end(),
        [](const std::unique_ptr<Label>& a, const std::unique_ptr<Label>& b) {
            return a->getDepth() < b->getDepth();
        });

    VkDescriptorSet dummy = dummyDescSet();

    for (auto& l : labels_) {
        if (!l->isVisible()) continue;
        l->buildMesh(buf, screenW, screenH);
        if (l->drawCall.indexCount == 0) continue;

        // Ensure solid-color draws always have a valid descriptor set bound.
        if (l->drawCall.state == 0 || l->drawCall.texture == VK_NULL_HANDLE)
            l->drawCall.texture = dummy;

        drawCalls_.push_back(l->drawCall);

        // Styled text spans one draw call per <b>/<i> run; null for everything else.
        if (const auto* extra = l->extraDrawCalls()) {
            for (UIDrawCall dc : *extra) {
                if (dc.state == 0 || dc.texture == VK_NULL_HANDLE) dc.texture = dummy;
                drawCalls_.push_back(dc);
            }
        }
    }
}

void UIManager::handleResize(float newW, float newH) {
    for (auto& l : labels_) l->onResize(newW, newH);
}

void UIManager::handleClick(float fx, float fy, int button, int action) {
    // Dispatch to the topmost (highest depth) label that passes the hit test.
    Label* hit = nullptr;
    for (auto& l : labels_) {
        if (l->isVisible() && l->hitTest(fx, fy)) {
            if (!hit || l->getDepth() > hit->getDepth())
                hit = l.get();
        }
    }
    if (hit) hit->onClicked(fx, fy, button, action);
}

VkDescriptorSet UIManager::dummyDescSet() const {
    return dummyTexture_ ? dummyTexture_->getDescriptorSet() : VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

Background* UIManager::addBackground(math::Vec2 min, math::Vec2 max,
                                     math::Vec4 color, int depth) {
    auto label = std::make_unique<Background>(min, max, color, depth);
    auto* ptr  = label.get();
    labels_.push_back(std::move(label));
    return ptr;
}

TexturedBackground* UIManager::addTexturedBackground(math::Vec2 min, math::Vec2 max,
                                                     math::Vec4 tint, Texture* tex, int depth) {
    auto label = std::make_unique<TexturedBackground>(min, max, tint, depth, tex);
    auto* ptr  = label.get();
    labels_.push_back(std::move(label));
    return ptr;
}

AnimatedBackground* UIManager::addAnimatedBackground(math::Vec2 min, math::Vec2 max,
                                                     math::Vec4 tint,
                                                     std::vector<Texture*> frames,
                                                     float fps, int depth) {
    auto label = std::make_unique<AnimatedBackground>(min, max, tint, depth,
                                                      std::move(frames), fps);
    auto* ptr  = label.get();
    labels_.push_back(std::move(label));
    return ptr;
}

CurvedBackground* UIManager::addCurvedBackground(math::Vec2 min, math::Vec2 max,
                                                  math::Vec4 color, float curvature, int depth) {
    auto label = std::make_unique<CurvedBackground>(min, max, color, depth, curvature);
    auto* ptr  = label.get();
    labels_.push_back(std::move(label));
    return ptr;
}

TextBox* UIManager::addTextBox(Background* parent, TextAtlas* atlas, const std::string& text,
                               int depth, int displayPx, Alignment align) {
    // `this` enables inline <b>/<i> styling — see TextBox's header comment.
    auto label = std::make_unique<TextBox>(parent, atlas, text, depth, displayPx, align, this);
    auto* ptr  = label.get();
    labels_.push_back(std::move(label));
    return ptr;
}

void UIManager::add(std::unique_ptr<Label> label) {
    labels_.push_back(std::move(label));
}

void UIManager::remove(Label* label) {
    if (!label) return;
    labels_.erase(
        std::remove_if(labels_.begin(), labels_.end(),
            [label](const std::unique_ptr<Label>& l) { return l.get() == label; }),
        labels_.end()
    );
}

TextAtlas* UIManager::loadAtlas(const std::string& fontPath, int pixelSize) {
    // MSDF atlases are resolution-independent, so one atlas per font (the
    // pixelSize arg is kept for call-site compatibility but no longer keys the
    // cache — keying by font path was the old size-only bug that returned the
    // wrong atlas in multi-font setups).
    for (auto& a : atlases_) {
        if (a->fontPath() == fontPath) return a.get();
    }
    // Failures are cached too. Styled-text runs ask for variants that often
    // aren't baked (there is no monaco_bold), and without this a single <b> tag
    // would retry the disk read and re-log the failure on every frame.
    if (failedPaths_.count(fontPath)) return nullptr;

    auto atlas = std::make_unique<TextAtlas>();
    if (!atlas->init(*gpu_, commandPool_, uiDescPool_, textureLayout_, fontPath, pixelSize)) {
        failedPaths_.insert(fontPath);
        return nullptr;
    }
    auto* ptr = atlas.get();
    atlases_.push_back(std::move(atlas));
    return ptr;
}

TextAtlas* UIManager::loadAtlasWithFallback(const std::string& variantPath,
                                            const std::string& fallbackPath) {
    if (auto* a = loadAtlas(variantPath)) return a;
    return loadAtlas(fallbackPath);
}
