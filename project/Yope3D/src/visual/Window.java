package visual;

import org.lwjgl.glfw.GLFW;
import org.lwjgl.opengl.GL;
import org.lwjgl.opengl.GL11;
import org.lwjgl.system.MemoryUtil;

public class Window {	
	//window variables
	//GLFW long handle id for window
	//basically an window ID that lets us use all the window specific functions GLFW has to offer
	private long window;
	//width and height of the specified window in pixels
	private int width;
	private int height;
	//name of the window in the taskbar window
	private String title;
	
	public Window(String title, int width, int height) {
		this.title = title;
		this.height = height;
		this.width = width;
	}
	
	//initialize this window instance
	//very important as this is needed to create the window and get the display running
	public void init() {
		//initialize glfw and get the result (successful or not)
		boolean res = GLFW.glfwInit();
		//fail information
		if(res == false) {
			System.err.println("GLFW couldn't initialize. Please try again");
			System.exit(0);
		}
		
		//window hints
		//very important because they indicate certain behaviors that we want our window to do to GLFW
		GLFW.glfwDefaultWindowHints();
		
		//whether or not the window should be visible
		//for now false (will enable later)
		GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GL11.GL_FALSE);
		//whether or not the window should be resizeable
		GLFW.glfwWindowHint(GLFW.GLFW_RESIZABLE, GL11.GL_TRUE);
		//what version of openGL will be running on the window (major and minor)
		GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR, 4);
		GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR, 2);
		//core profile of opengl
		//basically lets GLFW which opengl it should create the context for (so we can run it)
		GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_PROFILE, GLFW.GLFW_OPENGL_CORE_PROFILE);
		
		//now we create the window
		//first three specify width, height, and title
		//the 2nd to last parameter specifies which monitor to go on
		//the last memoryutil.null specifies that there is no share
		window = GLFW.glfwCreateWindow(width, height, title, GLFW.glfwGetPrimaryMonitor(), MemoryUtil.NULL);
		
		//basic key callback to close window when escape is hit
		//the 5 parameters are necessary in the lambda expression because that is what 
		//the key callback receives
		GLFW.glfwSetKeyCallback(window, (window, key, scancode, action, mods) -> { 
			//check if the key was escape and the action was release
			if(key == GLFW.GLFW_KEY_ESCAPE && action == GLFW.GLFW_RELEASE) {
				//then we should set that the window should close to true
				GLFW.glfwSetWindowShouldClose(window, true);
			}
		});
		
		//make the context
		//this enables us to use opengl (because with the context we can use the opengl library and it will write/affect the window)
		GLFW.glfwMakeContextCurrent(window);
		
		//but first we need to create the capabilities
		//just because it is affected doesn't mean we can use it just yet (with the capabilities call, we can though)
		GL.createCapabilities();
		
		//set the background color to be black
		//this is also known as clear color
		//also the first use of a opengl library call
		//from now on we can use the opengl library
		GL11.glClearColor(0, 0, 0, 0);
	}
	
	//swaps the read and write buffers
	//then looks for any action and processes it
	//essential to window updating
	public void update() {
		GLFW.glfwSwapBuffers(window);
		GLFW.glfwPollEvents();
	}
	
	//cleanup method
	//cleans up the window
	public void cleanup() {
		GLFW.glfwDestroyWindow(window);
	}
	
	//getter for width
	public int getWidth() {
		return width;
	}
	
	//getter for height
	public int getHeight() {
		return height;
	}
	
	//returns if the window should close yet
	public boolean shouldClose() {
		return GLFW.glfwWindowShouldClose(window);
	}
}
