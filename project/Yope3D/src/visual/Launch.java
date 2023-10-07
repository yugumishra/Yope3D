package visual;

import java.awt.Dimension;
import java.awt.Toolkit;

public class Launch {
	public static void main(String[] args) {
		//get width and height from the toolkit of the current monitor
		Dimension screen = Toolkit.getDefaultToolkit().getScreenSize();
		
		//initialize window using the width and height
		Window window = new Window("Yope3D", screen.width, screen.height);
		
		//create the loop instance
		Loop loop = new Loop(window);
		
		//start the loop
		loop.start();
	}
}
