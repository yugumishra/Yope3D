package visual;

import java.awt.Dimension;
import java.awt.Toolkit;

public class Launch {
	public static void main(String[] args) {
		//get width and height from the toolkit of the current monitor
		Dimension screen = Toolkit.getDefaultToolkit().getScreenSize();
		
		//initialize window using the width and height
		Window window = new Window("Yope3D", screen.width, screen.height);
		
		//create the World instance that represents objects in the world
		World world = new World();
		//populate the world instance with a mesh
		//this is done with triangulation, because we are rendering the triangles
		//each point is connected with a mesh of triangles that form a smooth surface when rendered
		float[] vertices = {
			-0.5f, 0.5f,
			-0.5f, -0.5f,
			0.5f, 0.5f,
			0.5f, 0.5f,
			-0.5f, -0.5f,
			0.5f, -0.5f
		};
		//creation of a mesh instance
		Mesh m = new Mesh(vertices);
		//addition of the mesh to the world
		world.addMesh(m);
		
		//create a Renderer instance, which is used to render the meshes in the World instance
		Renderer renderer = new Renderer();
		
		//create the loop instance
		Loop loop = new Loop(window, world, renderer);
		
		//start the loop
		loop.start();
	}
}
