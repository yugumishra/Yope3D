package main;

import java.awt.GraphicsDevice;
import java.awt.GraphicsEnvironment;
import visual.Launch;

public class Main {
	public static UIWindow window;

	public static void main(String[] args) {
		/*
		// get screen res
		GraphicsDevice gd = GraphicsEnvironment.getLocalGraphicsEnvironment().getDefaultScreenDevice();
		int width = gd.getDisplayMode().getWidth();
		int height = gd.getDisplayMode().getHeight();

		// create window
		window = new UIWindow(width, height);
		// init window
		window.init();

		// then start the loop
		window.start();
		*/
		Launch.launch();
	}
}