package visual;

import java.awt.Dimension;
import java.awt.Toolkit;

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
		//this is done with triangulation, because we are rendering the triangles
		//each point is connected with a mesh of triangles that form a smooth surface when rendered
		float[] vertices = {
			-0.5f, -0.5f, -1.2f,
			-0.5f, 0.5f, -1.2f,
			0.5f, 0.5f, -1f,
			0.5f, -0.5f, -1f
		};
		//now each point has 3 numbers defining it, x, y, and z
		//with the projection matrix the differing z values will be projected onto the plane
		//this will result in a 3d effect
		
		//now we create the indices that refer to the vertices to create triangles
		//this is how we don't repeat vertices
		int[] indices = {
				0, 1, 2,
				0, 2, 3
		};
		
		
		//creation of a mesh instance
		Mesh m = new Mesh(vertices, indices);
		//addition of the mesh to the world
		world.addMesh(m);
		
		//create a Renderer instance, which is used to render the meshes in the World instance
		renderer = new Renderer();
		
		//create the loop instance
		game = new Loop(window, world, renderer);
		
		//start the loop
		game.start();
	}
}
