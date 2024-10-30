package ui;

import org.joml.Vector2f;
import org.lwjgl.glfw.GLFW;
import visual.*;

public class UIInit {
	
	public static Loop game = Launch.game;
	public static Window window = Launch.window;
	public static World world = Launch.world;
	
	public static TextAtlas smallAtlas;
	public static TextAtlas titleAtlas;
	public static TextAtlas normalAtlas;
	public static int pxSize = 70;
	
	public static void init() {
		
		// initialize a text atlas
		
		smallAtlas = new TextAtlas("Assets\\fonts\\nunito_sans_semibold_italic.ttf", pxSize/2);
		normalAtlas = new TextAtlas("Assets\\fonts\\nunito_sans_semibold_italic.ttf", pxSize);
		titleAtlas = new TextAtlas("Assets\\fonts\\nunito_sans_semibold_italic.ttf", pxSize*2, "SPRINGDEMO");
		/*
		

		// initialize some labels
		AnimatedBackground abg = new AnimatedBackground(0, "Assets\\Textures\\background", ".png", 58) {
			@Override
			public void update() {
				increment();
			}
		}; abg.setVisible(true);
		
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
				Vector2f mi = Launch.window.windowToPixel(super.min, Launch.window.getWidth(), Launch.window.getHeight());
				Vector2f ma = Launch.window.windowToPixel(super.max, Launch.window.getWidth(), Launch.window.getHeight());
				if(x > mi.x && x < ma.x && y > mi.y && y < ma.y && action == GLFW.GLFW_PRESS) {
					//kill the ui window
					cleanup();
					GLFW.glfwSetWindowShouldClose(Launch.window.getWindow(), true);
					GLFW.glfwDestroyWindow(Launch.window.getWindow());
					
					//end the program
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
				Vector2f mi = Launch.window.windowToPixel(super.min, Launch.window.getWidth(), Launch.window.getHeight());
				Vector2f ma = Launch.window.windowToPixel(super.max, Launch.window.getWidth(), Launch.window.getHeight());
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
				Vector2f mi = Launch.window.windowToPixel(super.min, Launch.window.getWidth(), Launch.window.getHeight());
				Vector2f ma = Launch.window.windowToPixel(super.max, Launch.window.getWidth(), Launch.window.getHeight());
				if(x > mi.x && x < ma.x && y > mi.y && y < ma.y && action == GLFW.GLFW_PRESS && this.draw()) {
					this.setVisible(false);
					bg.setVisible(false);
					bg3.setVisible(true);
					bg4.setVisible(true);
				}
			}
		};
		

		Launch.window.addLabel(abg);
		Launch.window.addLabel(bg);
		Launch.window.addLabel(bg2);
		Launch.window.addLabel(bg3);
		Launch.window.addLabel(bg4);
		
		abg.setVisible(true);
		bg.setVisible(true);
		bg2.setVisible(true);
		bg3.setVisible(false);
		bg4.setVisible(false);
		
		TextBox title = new TextBox(abg, "YOPE3D", pxSize*3, TextBox.TEXT_CONFIGURATIONS.CENTERED, Launch.window.titleAtlas);
		TextBox test = new TextBox(bg, "PLAY:", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, Launch.window.atlas);
		TextBox test2 = new TextBox(bg2, "INSTRUCTIONS:", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, Launch.window.atlas);
		TextBox x = new TextBox(bg4, "X", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, Launch.window.atlas);
		TextBox instructions = new TextBox(bg3, "W, A, S, D -> forward, left, down, right\n\nSpace -> move up\nLeft Shift -> move down\n\nLeft Click -> pull\nRight Click -> push\n\nE -> spawn\nQ -> destroy\n\nTab -> pause\nEscape -> exit\n\nScroll -> Adjust field strength", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, Launch.window.atlas);
		
		Launch.window.addLabel(title);
		Launch.window.addLabel(test);
		Launch.window.addLabel(test2);
		Launch.window.addLabel(x);
		Launch.window.addLabel(instructions);
		*/
		
		
		Background timerBacking = new Background(0.0f, 0.0f, 0.0f, 0);
		timerBacking.setVisible(false);
		
		Background instructionsPage = new CurvedBackground(0.2f, 0.8f, 0.9f, 2) {
			@Override
			public void init() {
				super.min = new Vector2f(-0.6f, 0.2f);
				super.max = new Vector2f(0.6f, -0.2f);
				
				xs = 0.25f;
				redefineMesh();
				reload();
			}
		};
		
		Background playButton = new CurvedBackground(0.2f, 0.8f, 0.9f, 1) {
			int frame = 0;
			@Override
			public void init() {
				super.min = new Vector2f(-0.4f, -0.4f);
				super.max = new Vector2f(0.4f, -0.6f);
				
				xs = 0.75f;
				redefineMesh();
				reload();
			}
			
			@Override
			public void update() {
				if(frame != -1 && frame >= 2) {
					window.pause();
					this.visibility = true;
					frame = -1;
				}
				this.visibility = window.isPaused() && (!instructionsPage.visibility);
				if(frame != -1) frame++;
			}
			
			@Override
			public void clicked(int x, int y, int button, int action) {
				Vector2f min = Launch.window.windowToPixel(super.min, Launch.window.getWidth(), Launch.window.getHeight());
				Vector2f max = Launch.window.windowToPixel(super.max, Launch.window.getWidth(), Launch.window.getHeight());
				if(x > min.x && x < max.x && y > min.y && y < max.y && action == GLFW.GLFW_PRESS && this.draw()) {
					Launch.window.unpause();
				}
			}
		};
		
		
		
		Background instructions = new CurvedBackground(0.2f,0.8f,0.9f,1) {
			@Override
			public void init() {
				super.min = new Vector2f(-0.4f, -0.7f);
				super.max = new Vector2f(0.4f, -0.9f);
				
				xs = 0.75f;
				
				redefineMesh();
				reload();
			}
			
			@Override
			public void update() {
				this.visibility = playButton.visibility;
			}
			
			@Override
			public void clicked(int x, int y, int button, int action) {
				Vector2f min = Launch.window.windowToPixel(super.min, Launch.window.getWidth(), Launch.window.getHeight());
				Vector2f max = Launch.window.windowToPixel(super.max, Launch.window.getWidth(), Launch.window.getHeight());
				if(x > min.x && x < max.x && y > min.y && y < max.y && action == GLFW.GLFW_PRESS && this.draw()) {
					this.visibility = false;
					playButton.visibility = false;
					instructionsPage.visibility = true;
				}
			}
		};
		
		Background instructionsClose = new Background(0.2f, 0.8f, 0.9f, 4) {
			@Override
			public void init() {
				super.min = new Vector2f(0.55f, 0.2f);
				super.max = new Vector2f(0.6f, 0.1f);
				
				redefineMesh();
				reload();
			}
			
			@Override
			public void update() {
				this.visibility = instructionsPage.visibility;
			}
			
			@Override
			public void clicked(int x, int y, int button, int action) {
				Vector2f min = Launch.window.windowToPixel(super.min, Launch.window.getWidth(), Launch.window.getHeight());
				Vector2f max = Launch.window.windowToPixel(super.max, Launch.window.getWidth(), Launch.window.getHeight());
				if(x > min.x && x < max.x && y > min.y && y < max.y && action == GLFW.GLFW_PRESS && this.draw()) {
					this.visibility = true;
					playButton.visibility = true;
					instructionsPage.visibility = false;
				}
			}
		};
		
		
		
		instructionsClose.setVisible(false);
		instructionsPage.setVisible(false);
		
		Background title = new Background(0.0f, 0.0f, 0.0f, 0);
		title.setVisible(false);
		
		TextBox instruct = new TextBox(instructions, "INSTRUCTIONS:", pxSize/2, TextBox.TEXT_CONFIGURATIONS.CENTERED, smallAtlas, true) {
			@Override
			public void update() {
				this.draw = instructions.visibility;
			}
		};
		
		TextBox actualInstructions = new TextBox(instructionsPage, "Use WASD to move, Left Shift to Sprint. Use LMB to stop/start, and space to jump.", pxSize/2, TextBox.TEXT_CONFIGURATIONS.CENTERED, smallAtlas, false) {
			@Override
			public void update() {
				this.draw = instructionsPage.visibility;
			}
		};
		
		TextBox play = new TextBox(playButton, "PLAY:", pxSize/2, TextBox.TEXT_CONFIGURATIONS.CENTERED, smallAtlas, true) {
			@Override
			public void update() {
				this.draw = playButton.visibility;
			}
		};
		TextBox titleText = new TextBox(title, "SPRING DEMO", pxSize*2, TextBox.TEXT_CONFIGURATIONS.CENTERED, titleAtlas, true) {
			@Override
			public void update() {
				this.draw = window.isPaused();
			}
		};
		TextBox xButton = new TextBox(instructionsClose, "X", pxSize/2, TextBox.TEXT_CONFIGURATIONS.CENTERED, smallAtlas, false) {
			@Override
			public void update() {
				this.draw = instructionsClose.visibility;
			}
		};
		TextBox timer = new TextBox(timerBacking, "FPS: ", pxSize/2, TextBox.TEXT_CONFIGURATIONS.DEFAULT, smallAtlas, true) {
			@Override
			public void update() {
				if(window.isPaused()) return;
				float fps = Launch.game.getFPS();
				String msg = "FPS: " + (int) fps;
				//check and then set
				if(msg.equals(this.message) == false) {
					this.message = msg;
					this.generateTextMesh(TextBox.TEXT_CONFIGURATIONS.DEFAULT);
					this.reload();
				}
			}
		};
		
		Launch.window.addLabel(instructions);
		Launch.window.addLabel(instruct);
		Launch.window.addLabel(playButton);
		Launch.window.addLabel(play);
		Launch.window.addLabel(title);
		Launch.window.addLabel(titleText);
		Launch.window.addLabel(instructionsPage);
		Launch.window.addLabel(actualInstructions);
		Launch.window.addLabel(instructionsClose);
		Launch.window.addLabel(xButton);
		Launch.window.addLabel(timerBacking);
		Launch.window.addLabel(timer);
	}
	
	public static void deathScreen(int var) {
		window.clearUI();
		switch(var) {
		case 0:
			//fell off the map
			Background backing = new Background(0,0,0,0);
			backing.setVisible(false);
			TextBox winner = new TextBox(backing, "You fell off the map!", pxSize, TextBox.TEXT_CONFIGURATIONS.CENTERED, normalAtlas, true);
			Launch.window.addLabel(backing);
			Launch.window.addLabel(winner);
			break;
		default:
			break;
		}
	}
}
