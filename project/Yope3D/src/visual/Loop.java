package visual;

public class Loop {
	//time of the creation of the loop
	//will be used for time tracking (fps calculation)
	private long startTime;
	//window instance to update window
	private Window window;
	//world instance to access meshes
	private World world;
	//renderer instance to render meshes
	private Renderer renderer;
	//frame counter
	//we can use int here because the int storing capacity lasts well over a year of runtime on 60fps
	//no need for extra storage with long
	private int frames;
	//we will display localized fps, not global fps
	//so the below time variables is used for that purpose
	private long lastTime;

	//fps variable
	//encapsulates the current frames per second of the application
	private float fps;
	
	//constructor
	public Loop(Window w, World world, Renderer renderer) {
		startTime = System.currentTimeMillis();
		lastTime = System.currentTimeMillis();
		this.window = w;
		this.world = world;
		this.renderer = renderer;
	}
	
	//how one starts the loop
	public void start() {
		//first initialize anything necessary
		init();
		//then run
		run();
	}
	
	//initializes all necessary variables
	public void init() {
		window.init();
		renderer.init();
		world.init();
	}
	
	//run method
	//main loop of the window
	//basically runs forever until a signal is sent to stop (like hitting the escape key)
	//then stops updating the window which closes it
	public void run() {
		//infinite loop
		while(true) {
			//increment the frames
			frames++;
			if(frames % 10000 == 0) {
				//reupdate the fps
				//calculate the difference in time from last update to now
				float timeDiff = (float) (System.currentTimeMillis() - lastTime);
				//reupdate last time
				lastTime = System.currentTimeMillis();
				//frame difference will always be 10000, and time diff is measured in milliseconds
				//once you work out the math, the fps is just 10 million times the inverse in difference
				fps = 10000000 / timeDiff;
				
				//now we update the title to indicate FPS
				window.setTitle(window.getTitle() + " FPS: " + (int) fps);
			}
			
			//render the current state
			render();
			//update the state
			update();
			
			
			//if the window should close, break out of the loop
			//this stops the updating and, by extension, closes the window
			if(window.shouldClose()) {
				break;
			}
		}
	}
	
	//encapsulates all of the updates in the loop in one function
	public void update() {
		window.update();
	}
	
	//encapsulates all of the renderings that are done in the loop
	//the world instance is used to access each mesh, which is then rendered using the renderer
	public void render() {
		for(int i= 0; i< world.getNumMeshes(); i++) {
			Mesh m = world.getMesh(i);
			renderer.render(m);
		}
	}
	
	//getter for fps
	public float getFPS() {
		return fps;
	}
}
