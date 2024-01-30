package main;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

import org.joml.Vector2f;
import org.lwjgl.PointerBuffer;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.opengl.GL;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;
import org.lwjgl.util.freetype.FreeType;

import ui.AnimatedBackground;
import ui.Background;
import ui.CurvedBackground;
import ui.Label;
import ui.TextAtlas;
import ui.TextBox;

public class UIWindow {

	// handle to free type lib
	public PointerBuffer library;

	// ref to the texture atlas object holding the text texture atlas
	public TextAtlas atlas;
	
	// ref to the small texture atlas object holding the small text texture atlas
	public TextAtlas smallAtlas;
	
	// ref to the title texture atlas object
	public TextAtlas titleAtlas;

	// this section is for shader uniform names
	public static final String TEXTURED = "textured";

	// handle to GLFW window
	private long window;
	// width and height of window
	private int width;
	private int height;
	private int totalWidth;
	private int totalHeight;

	// uniform mapping
	private HashMap<String, Integer> uniforms;

	// program handle
	private int program;
	
	//frame counter
	int frames;

	// list of labels that is the UI
	// this list is sorted via depth
	// so the furthest back labels will be drawn first
	private List<ArrayList<Label>> ui;

	public UIWindow(int width, int height) {
		this.totalHeight = height;
		this.totalWidth = width;
		this.width = (3*width)/4;
		this.height = (3*height)/4;
		ui = new ArrayList<ArrayList<Label>>();
		uniforms = new HashMap<String, Integer>();
	}

	public void init() {
		// init glfw
		if (GLFW.glfwInit() == false) {
			System.err.println("GLFW failed to initialize. Try again");
			System.exit(0);
		}

		// set the window hints
		GLFW.glfwDefaultWindowHints();

		GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GL11.GL_FALSE);

		GLFW.glfwWindowHint(GLFW.GLFW_RESIZABLE, GL11.GL_TRUE);

		GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR, 4);
		GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR, 2);

		GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_PROFILE, GLFW.GLFW_OPENGL_CORE_PROFILE);

		window = GLFW.glfwCreateWindow(width, height, "heh", MemoryUtil.NULL, MemoryUtil.NULL);

		// center on screen
		GLFW.glfwSetWindowPos(window, totalWidth/2 - width/2, totalHeight/2 - height/2);

		// check if failed
		if (window == MemoryUtil.NULL) {
			System.err.println("Window failed to create. Please try again");
			System.exit(0);
		}

		// make exit callback
		GLFW.glfwSetKeyCallback(window, (window, key, scancode, action, mods) -> {
			if (key == GLFW.GLFW_KEY_ESCAPE && action == GLFW.GLFW_RELEASE) {
				// close window
				GLFW.glfwSetWindowShouldClose(window, true);
			}
		});

		// make window changed callback
		// so the opengl viewports maps to the window perfectly
		GLFW.glfwSetFramebufferSizeCallback(window, (window, width, height) -> {
			GL11.glViewport(0, 0, width, height);
			
			for (ArrayList<Label> layer : ui) {
				for (Label l : layer) {
					l.resizeUpdate(this.width, this.height, width, height);
				}
			}
			
			this.width = width;
			this.height = height;
		});
		
		GLFW.glfwSetMouseButtonCallback(window, (window, button, action, mods) -> {
			for(ArrayList<Label> layer: ui) {
				for(Label l: layer) {
					double[] x = new double[1];
					double[] y = new double[1];
					GLFW.glfwGetCursorPos(window, x, y);
					l.clicked((int) x[0], (int) y[0], button, action);
				}
			}
		});

		// make context current
		GLFW.glfwMakeContextCurrent(window);

		// init opengl
		GL.createCapabilities();

		// enable visibility
		GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GL11.GL_TRUE);
		GLFW.glfwShowWindow(window);

		// enable vsync
		GLFW.glfwSwapInterval(1);
		
		//set up alpha testi
		GL11.glEnable(GL11.GL_BLEND);
		GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);

		// set up rendering
		int vid = GL20.glCreateShader(GL20.GL_VERTEX_SHADER);
		String vertexShader = 
				  "#version 330 core\n" 
				+ "\n" 
				+ "layout (location = 0) in vec2 pos;\n" 
				+ "layout (location = 1) in vec2 Tex;\n"
				+ "\n"
				+ "uniform mat3 objMatrix;\n"
				+ "uniform int state;\n"
				+ "\n"
				+ "out vec2 tex;\n"
				+ "\n" 
				+ "void main() {\n" 
				+ "		if(state == 1) {\n"
				+ "			//text\n"
				+ "			vec3 res = objMatrix * vec3(pos, 1.0);\n"
				+ "			gl_Position = vec4(res, 1.0);\n"
				+ "		}else {\n"
				+ "			gl_Position = vec4(pos, 1.0, 1.0);\n"
				+ "		}"	
				+ "		tex = Tex;\n"
				+ "}\n";
		GL20.glShaderSource(vid, vertexShader);

		GL20.glCompileShader(vid);

		if (GL20.glGetShaderi(vid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			// uh oh
			System.err.println("Vertex shader failed to compile.");
			System.err.println(GL20.glGetShaderInfoLog(vid));
			GL20.glDeleteShader(vid);
			cleanup();
			System.exit(0);
		}

		int fid = GL20.glCreateShader(GL20.GL_FRAGMENT_SHADER);
		String fragmentShader = 
				  "#version 330 core\n" 
				+ "in vec2 tex;\n" 
				+ "\n" 
				+ "uniform vec3 col;\n"
				+ "uniform int state;\n" 
				+ "uniform sampler2D image;\n" 
				+ "\n" 
				+ "void main() {\n"
				+ "     if(state == 0) {\n" 
				+ "			gl_FragColor = vec4(col.x, col.y, col.z, 1.0);\n"
				+ "		}else if(state == 1){\n" 
				+ "			float texel = texture(image, tex).r;\n"
				+ "			vec3 resultant = vec3(col.x * texel, col.y * texel, col.z * texel);\n" 
				+ "			gl_FragColor = vec4(resultant, texel);\n"
				+ "		}else if(state == 2) {\n"
				+ "			vec4 texel = texture(image, tex);\n"
				+ "			vec3 resultant = col * texel.xyz;\n"
				+ "			gl_FragColor = vec4(resultant, texel.w);\n"
				+ "		}else {\n"
				+ "			gl_FragColor = vec4(0.0,0.0,0.0,1.0);\n"
				+ "		}\n"
				+ "}\n";

		GL20.glShaderSource(fid, fragmentShader);

		GL20.glCompileShader(fid);

		if (GL20.glGetShaderi(fid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			// uh oh 2
			System.err.println("Fragment shader failed to compile");
			System.err.println(GL20.glGetShaderInfoLog(fid));
			GL20.glDeleteShader(fid);
			cleanup();
			System.exit(0);
		}

		// link them together
		program = GL20.glCreateProgram();

		GL20.glAttachShader(program, vid);
		GL20.glAttachShader(program, fid);

		GL20.glLinkProgram(program);

		if (GL20.glGetProgrami(program, GL20.GL_LINK_STATUS) == GL20.GL_FALSE) {
			// uh oh 3
			System.err.println("Program failed to link");
			System.err.println(GL20.glGetProgramInfoLog(program));

			GL20.glDeleteShader(vid);
			GL20.glDeleteShader(fid);
			GL20.glDeleteProgram(program);

			cleanup();
			System.exit(0);
		}

		// discard the shaders, the executable has already been created
		GL20.glDeleteShader(vid);
		GL20.glDeleteShader(fid);

		// validate the program
		GL20.glValidateProgram(program);

		// use this program
		GL30.glUseProgram(program);

		// now that rendering has been setup, we can render stuff
		addUniform("col");
		addUniform("state");
		addUniform("objMatrix");
		addUniform("image");

		// free type setup
		library = MemoryUtil.memAllocPointer(1);
		int error = FreeType.FT_Init_FreeType(library);
		if (error != 0) {
			System.err.println("Could not initialize FreeType");
			cleanup();
			System.exit(0);
		}
		int pxSize = 70;
		// initialize a text atlas
		atlas = new TextAtlas("Assets\\fonts\\nunito_sans_semibold_italic.ttf", pxSize);
		smallAtlas = new TextAtlas("Assets\\fonts\\montserrat_bold.ttf", pxSize/2);
		titleAtlas = new TextAtlas("Assets\\fonts\\nunito_sans_semibold_italic.ttf", pxSize*3, "YOPE3D");

		// initialize some labels
		AnimatedBackground abg = new AnimatedBackground(0, "Assets\\Textures\\background", ".png", 58) {
			@Override
			public void update() {
				increment();
			}
		};
		
		Background bg = new CurvedBackground(1.0f, 0.0f, 0.0f, 2) {
			@Override
			public void init() {
				super.min = new Vector2f(-0.3f, -0.4f);
				super.max = new Vector2f(0.3f, -0.6f);
				
				xs = 1.0f;
				
				redefineMesh();
				
				reload();
			}
			
			@Override
			public void clicked(int x, int y, int button, int action) {
				Vector2f mi = windowToPixel(super.min, width, height);
				Vector2f ma = windowToPixel(super.max, width, height);
				if(x > mi.x && x < ma.x && y > mi.y && y < ma.y && action == GLFW.GLFW_PRESS) {
					//kill the ui window
					cleanup();
					GLFW.glfwSetWindowShouldClose(window, true);
					GLFW.glfwDestroyWindow(window);
					
					//init yope
					visual.Launch.launch();
					
					//just in case
					System.exit(0);
				}
			}
		};
		
		Background bg3 = new CurvedBackground(0.5f, 0.5f, 0.5f, 3) {
			@Override
			public void init() {
				super.min = new Vector2f(-0.5f, 0.95f);
				super.max = new Vector2f(0.5f, -0.95f);
				
				xs = 0.75f;
				
				redefineMesh();
				reload();
			}
		};
		
		Background bg4 = new Background(0.2f, 0.2f, 0.2f, 4) {
			@Override
			public void init() {
				super.min = new Vector2f(0.4f, 0.95f);
				super.max = new Vector2f(0.5f, 0.8f);
				
				redefineMesh();
				reload();
			}
			
			@Override
			public void clicked(int x, int y, int button, int action) {
				Vector2f mi = windowToPixel(super.min, width, height);
				Vector2f ma = windowToPixel(super.max, width, height);
				if(x > mi.x && x < ma.x && y > mi.y && y < ma.y && action == GLFW.GLFW_PRESS && this.draw()) {
					//hide instructions
					this.setVisible(false);
					bg3.setVisible(false);
					bg.setVisible(true);
				}
			}
		};
		
		
		
		Background bg2 = new CurvedBackground(1.0f, 0.0f, 0.0f, 2) {
			@Override
			public void init() {
				super.min = new Vector2f(-0.3f, -0.7f);
				super.max = new Vector2f(0.3f, -0.9f);
				
				xs = 1.0f;
				
				redefineMesh();
				reload();
			}
			
			@Override
			public void update() {
				if(bg.draw()) {
					if(draw() == false) this.setVisible(true);
				}else {
					if(draw()) this.setVisible(false);
				}
			}
			
			@Override
			public void clicked(int x, int y, int button, int action) {
				Vector2f mi = windowToPixel(super.min, width, height);
				Vector2f ma = windowToPixel(super.max, width, height);
				if(x > mi.x && x < ma.x && y > mi.y && y < ma.y && action == GLFW.GLFW_PRESS && this.draw()) {
					this.setVisible(false);
					bg.setVisible(false);
					bg3.setVisible(true);
					bg4.setVisible(true);
				}
			}
		};
		

		addLabel(abg);
		addLabel(bg);
		addLabel(bg2);
		addLabel(bg3);
		addLabel(bg4);
		
		abg.setVisible(true);
		bg.setVisible(true);
		bg2.setVisible(true);
		bg3.setVisible(false);
		bg4.setVisible(false);
		
		TextBox title = new TextBox(abg, "YOPE3D", pxSize*3, TextBox.TEXT_CONFIGURATIONS.CENTERED, titleAtlas);
		TextBox test = new TextBox(bg, "PLAY:", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, atlas);
		TextBox test2 = new TextBox(bg2, "INSTRUCTIONS:", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, atlas);
		TextBox x = new TextBox(bg4, "X", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, atlas);
		TextBox instructions = new TextBox(bg3, "W, A, S, D -> forward, left, down, right\n\nSpace -> move up\nLeft Shift -> move down\n\nLeft Click -> pull\nRight Click -> push\n\nE -> spawn\nQ -> destroy\n\nTab -> pause\nEscape -> exit\n\nScroll -> Adjust field strength", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, atlas);
		
		addLabel(title);
		addLabel(test);
		addLabel(test2);
		addLabel(x);
		addLabel(instructions);
		
		
	}

	public void addUniform(String name) {
		int location = GL20.glGetUniformLocation(program, name);

		if (location < 0) {
			System.err.println("Uniform " + name + " not found");
			System.err.println(GL20.glGetError());
			return;
		}
		uniforms.put(name, location);
	}

	public int getUniform(String name) {
		return uniforms.get(name);
	}

	// this method starts the render loop
	public void start() {
		// this is the basic loop
		while (this.shouldClose() == false) {
			update();
			GL20.glClear(GL20.GL_COLOR_BUFFER_BIT | GL20.GL_DEPTH_BUFFER_BIT);
			// render & update each label
			for (ArrayList<Label> layer : ui) {
				for (Label label : layer) {
					if(label.draw()) {
						label.render();
					}
					label.update();
				}
			}
			
			frames++;
		}

		// cleanup the labels
		for (ArrayList<Label> layer : ui) {
			for (Label label : layer) {
				label.cleanup();
			}
		}
		// cleanup the window
		cleanup();
	}

	public void update() {
		GLFW.glfwSwapBuffers(window);
		GLFW.glfwPollEvents();
	}

	public void cleanup() {
		GLFW.glfwDestroyWindow(window);
		GLFW.glfwTerminate();
	}

	public boolean shouldClose() {
		return GLFW.glfwWindowShouldClose(window);
	}

	public void addLabel(Label l) {
		if (ui.isEmpty() || l.getDepth() >= ui.size()) {
			for(int i =0; i< l.getDepth() - ui.size(); i++) {
				ui.add(new ArrayList<Label>());
			}
			ArrayList<Label> finalLayer = new ArrayList<Label>();
			finalLayer.add(l);
			ui.add(finalLayer);
		}else if(l.getDepth() < ui.size()) {
			ui.get(l.getDepth()).add(l);
			
		}
	}

	// pixel to window coordinate transformation
	public Vector2f pixelToWindow(int x, int y, int width, int height) {
		float X = 2 * (float) x / (float) width;
		float Y = 2 * (float) y / (float) height;

		return new Vector2f(X - 2, 2 - Y);
	}
	
	//same as above but vector2f format
	public Vector2f pixelToWindow(Vector2f vector, int width, int height) {
		float X = 2 * vector.x / (float) width;
		float Y = 2 * vector.y / (float) height;
		
		return new Vector2f(X - 1, 1 - Y);
	}
	
	// window to pixel coordinate transformation
	public Vector2f windowToPixel(float x, float y, int width, int height) {
		float X = x + 1;
		X *= (float) width/2;
		
		float Y = 1 - y;
		Y *= height/2;
		
		
		
		//round here to get rid of any floating point error
		return new Vector2f(Math.round(X), Math.round(Y));
	}
	
	//same as above but vector2f format
	public Vector2f windowToPixel(Vector2f vector, int width, int height) {
		float X = vector.x + 1;
		X *= (float) width/2;
		
		float Y = 1 - vector.y;
		Y *= height/2;
		
		
		
		return new Vector2f(Math.round(X), Math.round(Y));
	}
	
	public int getWidth() {
		return width;
	}
	
	public int getHeight() {
		return height;
	}
}

