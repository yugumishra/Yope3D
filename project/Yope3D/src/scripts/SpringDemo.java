package scripts;

import org.joml.Math;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Vector4f;
import org.lwjgl.glfw.GLFW;

import audio.Listener;
import physics.BoundedBarrier;
import physics.CSphere;
import physics.Raycast;
import physics.Sphere;
import physics.Spring;
import visual.Launch;
import visual.Mesh;
import visual.PointLight;
import visual.Util;
import visual.Util.STATES;

public class SpringDemo extends Script{
	
	Mesh cameraCollider;
	Mesh[][] cloth;
	
	int clickFrame;
	int clickFrame2;
	
	@Override
	public void init() {
		super.init();
		
		//add a floor
		Mesh floor = Util.readObjFile("Assets\\Models\\plane.obj");
		floor.setState(STATES.TEXTURED);
		floor.setTexture("Assets\\Textures\\brick.jpg");
		floor.getHull().fix();
		floor.loadMesh();
		world.addMesh(floor);
		
		world.instantiateCollisionTree(new Vector3f(-100, 0, -100), new Vector3f(100, 100, 100), 3);
		
		//add the world barriers
		world.addBarrier(new BoundedBarrier(55, 55, new Vector3f(0, 1,0), new Vector3f(1,0,0), new Vector3f(0,0,0)));
		world.addBarrier(new BoundedBarrier(55, 55, new Vector3f( 1,0,0), new Vector3f(0,1,0), new Vector3f(-50, 50, 0)));
		world.addBarrier(new BoundedBarrier(55, 55, new Vector3f(-1,0,0), new Vector3f(0,1,0), new Vector3f( 50, 50, 0)));
		world.addBarrier(new BoundedBarrier(55, 55, new Vector3f(0,0, 1), new Vector3f(0,1,0), new Vector3f(0, 50, -50)));
		world.addBarrier(new BoundedBarrier(55, 55, new Vector3f(0,0,-1), new Vector3f(0,1,0), new Vector3f(0, 50,  50)));
		
		
		//add the camera collider
		cameraCollider = Sphere.genSphere(0, 0.5f);
		cameraCollider.redefineHull(new CSphere(1.0f, 0.5f, new Vector3f(0, 2, 25), new Vector3f(0,0,-0.1f), new Vector3f(), new Vector3f()));
		cameraCollider.setDraw(false);
		world.addMesh(cameraCollider);
		
		loop.getCamera().setPosition(cameraCollider.getHull().getPosition());
		loop.getCamera().setMoveSpeed(1f);
		
		
		//set the world time step
		world.setDT(1.0f/100.0f);
		
		world.addLight(new PointLight(new Vector3f( 40,50,-40), new Vector3f(1,1,1), new Vector3f(3f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f( 40,50, 40), new Vector3f(1,1,1), new Vector3f(3f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f(-40,50, 40), new Vector3f(1,1,1), new Vector3f(3f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f(-40,50, -40), new Vector3f(1,1,1), new Vector3f(3f,0.01f, 0.00001f)));
		
		Launch.world.lightChanged();
		
		//init listener
		Listener.init(cameraCollider.getHull().getPosition(), new Vector3f(), new Vector3f(0,0,-1), new Vector3f(0,1,0), 1.0f);
	}
	@Override
	public void update() {
		//input
		if((loop.getKey(GLFW.GLFW_KEY_W) || loop.getKey(GLFW.GLFW_KEY_A) || loop.getKey(GLFW.GLFW_KEY_S) || loop.getKey(GLFW.GLFW_KEY_D) || loop.getKey(GLFW.GLFW_KEY_SPACE) || loop.getKey(GLFW.GLFW_KEY_LEFT_CONTROL))) {
			Vector4f moveDir = new Vector4f(0,0,0,1);
			if(loop.getKey(GLFW.GLFW_KEY_W)) {
				moveDir.z -= 1;
			}
			if(loop.getKey(GLFW.GLFW_KEY_S)) {
				moveDir.z += 1;
			}
			if(loop.getKey(GLFW.GLFW_KEY_A)) {
				moveDir.x -= 1;
			}
			if(loop.getKey(GLFW.GLFW_KEY_D)) {
				moveDir.x += 1;
			}
			if (loop.getKey(GLFW.GLFW_KEY_SPACE)) {
				cameraCollider.getHull().addVelocity(new Vector3f(0, .25f, 0));
			}
			if (loop.getKey(GLFW.GLFW_KEY_LEFT_CONTROL)) {
				cameraCollider.getHull().addVelocity(new Vector3f(0,-1, 0));
			}
			
			//use the inverse camera to get into world space
			Matrix4f inv = loop.getCamera().genViewMatrix().transpose();
			moveDir.mul(inv);
			Vector3f forwardRay = new Vector3f(moveDir.x, moveDir.y, moveDir.z);
			
			forwardRay.mul(new Vector3f(1, 0, 1));
			
			if(Math.abs(forwardRay.x) >0.001f && Math.abs(forwardRay.z)> 0.001f) {
				forwardRay.normalize();
			}
			
			//move the camera collider in this direction
			forwardRay.mul(loop.getCamera().getMoveSpeed());
			if(loop.getKey(GLFW.GLFW_KEY_LEFT_SHIFT)) {
				forwardRay.mul(2.0f);
			}
			cameraCollider.getHull().addVelocity(forwardRay);
		}
		//add some friction to camera velocity
		Vector3f vel = cameraCollider.getHull().getVelocity();
		vel.x *= 0.9f;
		vel.z *= 0.9f;
		cameraCollider.getHull().setVelocity(vel);
		
		//cap camera rotation
		Vector3f cameraRot = new Vector3f(loop.getCamera().getRotation());
		cameraRot.x = Math.min(cameraRot.x,  3.1415926535f/2);
		cameraRot.x = Math.max(cameraRot.x, -3.1415926535f/2);
		loop.getCamera().setRotation(cameraRot);
		
		//advance world
		world.advance();
		
		
		//update cam and send state
		loop.getCamera().setPosition(cameraCollider.getHull().getPosition());
		loop.getCamera().update();
		loop.getCamera().sendState();
		
		
		//initial spring mass initialization
		if (loop.getForwardMB() && loop.frames() % 4 == 0) {
			//create the cloth holder
			cloth = new Mesh[30][30];
			
			//instantiate each component with a drawable mesh
			for (int j = 0; j < cloth.length; j++) {
				for (int i = 0; i < cloth.length; i++) {
					//built in cube function
					Mesh cube = Mesh.cube();
					
					//set it to vary color on a spectrum
					cube.setColor((i/30.0f), (j/30.0f), (0.4f + (i+j)/120.0f));
					//indicate rendering style
					cube.setState(STATES.SOLID_COLOR);
					
					//set initial position
					cube.getHull().setPosition(new Vector3f(-cloth.length + 2 * i, 75 - 2 * j, 0));
					
					//load and add to world
					cube.loadMesh();
					world.addMesh(cube);
					
					//place into holder
					cloth[i][j] = cube;
					
					//set top row to be fixed in place
					if (j == 0)
						cloth[i][j].getHull().fix();
				}
			}
			
			//instantiate the spring connections between each component and the next one (diagonals included)
			for (int j = 0; j < cloth.length; j++) {
				for (int i = 0; i < cloth.length - 1; i++) {
					initSpring(cloth[i][j], cloth[i + 1][j]);
					initSpring(cloth[j][i], cloth[j][i + 1]);
				}
			}

		}
		
		
		
		//pushing action
		if (loop.getRMB() && loop.frames() % 4 == 0) {
			// get a forward ray
			Matrix4f invCam = loop.getCamera().genViewMatrix().transpose();
			Vector4f forward = new Vector4f(0, 0, -1, 1);
			forward.mul(invCam);
			
			//create and normalize
			Vector3f forwardRay = new Vector3f(forward.x, forward.y, forward.z);
			forwardRay.normalize();

			// check for each mesh in the sphere
			for (int i = 1; i < cloth.length - 1; i++) {
				for (int j = 1; j < cloth.length - 1; j++) {
					Mesh m = cloth[i][j];
					//check for a hit against this component
					float hit = Raycast.raycastSphere(forwardRay, loop.getCamera().getPosition(), m.getHull().getPosition(),
							1.0f);
					if (hit != -1.0f) {
						// hit successful
						// how much to push the component forward by
						Vector3f push = new Vector3f(forwardRay).mul(30.0f);
						
						//apply the push to the intersecting component
						m.getHull().addVelocity(push);
						
						//find the neighboring components
						Mesh next = cloth[i + 1][j];
						Mesh down = cloth[i][j + 1];
						Mesh behind = cloth[i - 1][j];
						Mesh up = cloth[i][j - 1];
						
						//apply the push to the components around the intersection
						next.getHull().addVelocity(new Vector3f(push).div(next.getHull().getMass()));
						down.getHull().addVelocity(new Vector3f(push).div(down.getHull().getMass()));
						behind.getHull().addVelocity(new Vector3f(push).div(behind.getHull().getMass()));
						up.getHull().addVelocity(new Vector3f(push).div(up.getHull().getMass()));
					}
					
					
				}
			}
			
			
		}
	}
	
	public void initSpring(Mesh one, Mesh two) {
		//initialize the spring between the 2 meshes to 500 N/m and a rest length of 2m
		Spring spring = Spring.spring(one, two, 500.0f, 2.0f, 0);
		//set the spring to be invisible
		spring.loadMesh();
		spring.setDraw(false);
		//add it to the world for physics updates
		world.addMesh(spring);
	}
}
