package visual;

public class Loop {
	//time of the creation of the loop
	//will be used for time tracking (fps calculation)
	private long startTime;
	//window instance to update window
	private Window window;
	
	//constructor
	public Loop(Window w) {
		this.window = w;
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
	}
	
	//run method
	//main loop of the window
	//basically runs forever until a signal is sent to stop (like hitting the escape key)
	//then stops updating the window which closes it
	public void run() {
		while(true) {
			window.update();
			
			if(window.shouldClose()) {
				break;
			}
		}
	}
}
