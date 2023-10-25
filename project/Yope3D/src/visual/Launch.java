package visual;

import java.awt.Dimension;
import java.awt.Toolkit;

import org.joml.Vector3f;

import physics.Sphere;

public class Launch {
	//create static variables that hold the important variables concerning the application
	//so they can be accessed by other classes to run methods or get data
	public static Window window;
	public static World world;
	public static Renderer renderer;
	public static Loop game;
	
	public static void main(String[] args) {
		//get width and height from the toolkit of the current monitor
		Dimension screen = Toolkit.getDefaultToolkit().getScreenSize();
		
		//initialize window using the width and height
		window = new Window("Yope3D", screen.width, screen.height);
		
		//create the World instance that represents objects in the world
		world = new World();
		//populate the world instance with a mesh
		
		//creation of a mesh instance
		//Sphere m = Sphere.genSphere(1.0f, 3);
		Mesh m2 = Util.readObjFile("Assets\\Models\\floor.obj");
		Sphere sphere = Sphere.genSphere(1, 4);
		sphere.translate(new Vector3f(0,1,0));
		//addition of the meshes to the world
		//world.addMesh(m);
		world.addMesh(m2);
		world.addMesh(sphere);
		
		//create a Renderer instance, which is used to render the meshes in the World instance
		renderer = new Renderer();
		
		//create the loop instance
		game = new Loop(window, world, renderer);
		
		//start the loop
		game.start();
	}
}
