package scripts;

import visual.Launch;
import visual.Loop;
import visual.Renderer;
import visual.World;

public abstract class Script {
	//public so all script subclasses can access them without any imports
	public Loop loop;
	public World world;
	public Renderer renderer;
	
	
	public void init() {
		loop = Launch.game;
		world = Launch.world;
		renderer = Launch.renderer;
	}
	
	public abstract void update();
	
	//leave empty for overriding scripts to implement
	public void scrolled(double xOffset, double yOffset) {
		
	}
}
