package scripts;

import org.joml.Math;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Vector4f;
import org.lwjgl.glfw.GLFW;

import physics.Barrier;
import physics.BarrierHull;
import physics.BoundedBarrier;
import physics.COBB;
import physics.CSphere;
import physics.Collider;
import physics.Hull;
import physics.Raycast;
import physics.Sphere;
import physics.Spring;
import visual.Mesh;
import visual.PointLight;
import visual.Util;
import visual.Util.STATES;

public class Platformer extends Script {
	Mesh floor;
	Mesh[][] cloth;
	Sphere cameraCollider;
	Mesh one;
	Mesh two;
	Mesh[] course;
	Mesh star;
	Mesh[] starParticles;
	boolean dashing = false;
	int startFrame = 0;
	int fovLerp = 0;
	float fovIncrement = 0.0f;
	
	//publicly accessible variable to determine whether winning or not (used in ui)
	public int winningVariable = -1;

	@Override
	public void init() {
		super.init();

		// add the bounding box
		Barrier[] barriers = { new Barrier(new Vector3f(0, 1, 0), new Vector3f(0, 1, 0)),
				/*new BoundedBarrier(50, 50, new Vector3f(0, -1, 0), new Vector3f(1,0,0), new Vector3f(0, 100, 0)),
				
				new BoundedBarrier(25, 50, new Vector3f(1, 0, 0), new Vector3f(0,1,0), new Vector3f(-50, 0, -25)),
				new BoundedBarrier(25, 50, new Vector3f(1, 0, 0), new Vector3f(0,1,0), new Vector3f(-50, 0,  25)),
				new BoundedBarrier(50, 50, new Vector3f(-1, 0, 0), new Vector3f(0,1,0), new Vector3f(50, 50, 0)),
				new BoundedBarrier(50, 50, new Vector3f(0, 0, 1), new Vector3f(0,1,0), new Vector3f(0, 50, -50)),
				
				new BoundedBarrier(50, 50, new Vector3f(0, 0, -1), new Vector3f(0,1,0), new Vector3f(0, 50, 50)),*/ };
		for (Barrier b : barriers) {
			world.addBarrier(b);
		}
		
		world.instantiateCollisionTree(new Vector3f(-50, 0, -50), new Vector3f(50, 75, 50), 3);
		course = new Mesh[51];
		float radius = 15.0f; // Initial radius of the spiral
		float angleIncrement = 0.3f; // Angle increment to control the density of the spiral

		for (int i = 0; i < course.length-1; i++) {
		    //calc position 
		    float x = radius * (float) Math.cos(angleIncrement * i);
		    float z = radius * (float) Math.sin(angleIncrement * i);
		    float y = 0.5f + 2 * i; // y function

		    //create the barriers
		    BarrierHull bh = BoundedBarrier.genRectangularBarriers(new Vector3f(2, 2, 2), new Vector3f(x, y, z));
		    world.addObject(bh);

		    //create and position the cube mesh
		    Mesh cube = Mesh.cube();
		    cube.setScale(2.0f);
		    cube.getHull().setPosition(new Vector3f(x, y, z));
		    cube.setState(STATES.SOLID_COLOR);
		    cube.setColor((float) Math.random(), (float) Math.random(), (float) Math.random());
		    cube.loadMesh();
		    cube.getHull().fix();
		    world.addMesh(cube);
		    course[i] = cube;
		    radius += 0.5f;
		}
		BarrierHull bh = BoundedBarrier.genRectangularBarriers(new Vector3f(2, 2, 2), new Vector3f(-50f, 65, 0));
		world.addObject(bh);
		Mesh m = Mesh.cube();
		m.setScale(2.0f);
		m.getHull().setPosition(new Vector3f(-50f, 65, 0));
		m.setState(STATES.SOLID_COLOR);
		m.setColor((float) Math.random(), (float) Math.random(), (float) Math.random());
		m.loadMesh();
		m.getHull().fix();
		world.addMesh(m);
		course[course.length-1] = m;
		

		cameraCollider = Sphere.genSphere(1, 1f);
		cameraCollider.getHull().setPosition(new Vector3f(0, 120, 10));
		cameraCollider.setColor(1.0f, 0.0f, 1.0f);
		cameraCollider.setState(STATES.SOLID_COLOR);
		cameraCollider.setDraw(false);
		world.addMesh(cameraCollider);

		floor = Util.readObjFile("Assets\\Models\\plane.obj");
		floor.redefineHull(
				new CSphere(1.0f, Float.MAX_VALUE, new Vector3f(), new Vector3f(), new Vector3f(), new Vector3f()));
		floor.getHull().fix();

		floor.setColor(0.2f, 0.8f, 0.9f);
		floor.setScale(50.0f);
		floor.setState(STATES.SOLID_COLOR);
		floor.setDraw(true);
		
		star = Util.readObjFile("Assets\\Models\\star.obj");
		star.redefineHull(new CSphere(1.0f, 1.0f, new Vector3f(), new Vector3f(), new Vector3f(), new Vector3f()));
		star.getHull().fix();
		
		star.setColor(1.0f, 0.813437f, 0);
		star.setState(STATES.SOLID_COLOR);
		star.setDraw(true);
		star.loadMesh();
		world.addMesh(star);
		

		floor.loadMesh();
		world.addMesh(floor);

		loop.getCamera().setMoveSpeed(1f);
		loop.getCamera().setRotation(new Vector3f(-3.1415926535897f/2, 0, 0));

		world.addLight(new PointLight(new Vector3f(10000, 10000, 10000), new Vector3f(1,1,1), new Vector3f(1,0,0)));

		world.setCollisions(1);

		world.setDT(1.0f / 60.0f);
		
	}

	@Override
	public void update() {
		int platform = jumpValid();
		//cameraCollider.getHull().setVelocity(new Vector3f(0,0,0));
		if (loop.getKey(GLFW.GLFW_KEY_W) || loop.getKey(GLFW.GLFW_KEY_A) || loop.getKey(GLFW.GLFW_KEY_S)
				|| loop.getKey(GLFW.GLFW_KEY_D) || loop.getKey(GLFW.GLFW_KEY_SPACE)
				|| loop.getKey(GLFW.GLFW_KEY_LEFT_SHIFT)) {
			float mul = 1.0f;
			if (loop.getKey(GLFW.GLFW_KEY_LEFT_SHIFT))
				mul = 2.0f;
			if (loop.getKey(GLFW.GLFW_KEY_SPACE) && (cameraCollider.getHull().getPosition().y < 2.1f || platform != -1)) {
				cameraCollider.getHull().addVelocity(new Vector3f(0, loop.getCamera().getMoveSpeed() * mul, 0));
			}
			if (loop.getKey(GLFW.GLFW_KEY_LEFT_CONTROL)) {
				cameraCollider.getHull().addVelocity(new Vector3f(0, -loop.getCamera().getMoveSpeed() * mul, 0));
			}
			Vector4f moveDir = new Vector4f(0, 0, 0, 1);

			if (loop.getKey(GLFW.GLFW_KEY_W)) {
				moveDir.z += -1;
			}
			if (loop.getKey(GLFW.GLFW_KEY_S)) {
				moveDir.z += 1;
			}
			if (loop.getKey(GLFW.GLFW_KEY_A)) {
				moveDir.x += -1;
			}
			if (loop.getKey(GLFW.GLFW_KEY_D)) {
				moveDir.x += 1;
			}

			Matrix4f mat = loop.getCamera().genViewMatrix();
			mat.transpose();
			moveDir.mul(mat);

			Vector3f push = new Vector3f(moveDir.x, moveDir.y, moveDir.z);

			Vector3f up = new Vector3f(0, 1, 0);
			push.sub(up.mul(push.dot(up)));
			
			if(Math.abs(push.x) >0.001f && Math.abs(push.z)> 0.001f) {
				push.normalize();
			}

			push.mul(mul);

			push.mul(loop.getCamera().getMoveSpeed());

			cameraCollider.getHull().addVelocity(push);
			
		}

		// frictional decay
		Vector3f n = cameraCollider.getHull().getVelocity();
		n.x *= 0.8f;
		n.z *= 0.8f;
		cameraCollider.getHull().setVelocity(n);
		//cache the camera's true rotation prior to update
		Vector3f rot = loop.getCamera().getRotation();
		rot.sub(cameraCollider.getHull().getRotation());
		
		//cap the camera's rotation
		rot.x = Math.min(rot.x, 3.14159f/2);
		rot.x = Math.max(rot.x, -3.14159f/2);
		
		
		
		//rotate and sin the star
		star.getHull().setPosition(new Vector3f(-50f, 69 + (float) Math.sin(3 * loop.getTime()), 0));
		star.getHull().setRotation(new Vector3f(star.getHull().getRotation()).add(new Vector3f(0,4 * world.getDT(), 0)));
		
		//advance the world
		world.advance();
		
		loop.getCamera().setPosition(cameraCollider.getHull().getPosition());
		//add the camera collider's rot to the true rot and set it back into camera
		rot.add(cameraCollider.getHull().getRotation());
		loop.getCamera().setRotation(rot);
		
		

		loop.getCamera().update();

		loop.getCamera().sendState();
		
		/*
		// dashing code
		if(loop.getKey(GLFW.GLFW_KEY_F) && loop.frames() - startFrame > 240 && loop.frames() % 4 == 0 && !dashing) {
			dashing = true;
			startFrame = loop.frames();
			loop.getCamera().setMoveSpeed(loop.getCamera().getMoveSpeed() * 4);
			fovIncrement = 0.5f * loop.getCamera().getFOV()/60.0f;
			
		}
		if(dashing && fovLerp < 31) {
			loop.getCamera().setFOV(loop.getCamera().getFOV() + fovIncrement);
			fovLerp++;
		}
		if(dashing && loop.frames() - startFrame > 31) {
			//dash forward
			loop.getCamera().setMoveSpeed(loop.getCamera().getMoveSpeed() / 4.0f);
			loop.getCamera().setFOV(loop.getCamera().getFOV() - fovIncrement);
			fovLerp--;
			dashing = false;
		}
		if(fovLerp > 0 && !dashing) {
			loop.getCamera().setFOV(loop.getCamera().getFOV() - fovIncrement);
			fovLerp--;
		}
		
		//star check code
		if(platform == course.length - 1) {
			//we got the star
			star.setDraw(false);
			//instantiate the star particles
			if(starParticles == null) {
				starParticles = new Mesh[50];
				for(int i =0; i< starParticles.length; i++) {
					star = Util.readObjFile("Assets\\Models\\star.obj");
					float theta = (float) Math.random() * 3.1415926535897f * 0.5f + 3.1415926535897f/4.0f;
					float phi = (float) Math.random() * 3.1415926535897f * 2;
					float y = (float) Math.sin(theta);
					float nar = (float) Math.sin(theta);
					float x = (float) Math.cos(phi) * nar;
					float z = (float) Math.sin(phi) * nar;
					Vector3f dir = new Vector3f(x,y,z);
					dir.mul(3.0f);
					star.redefineHull(new CSphere(1.0f, 0.2f, new Vector3f(new Vector3f(cameraCollider.getHull().getPosition()).add(dir)), new Vector3f(dir.mul(0.25f)), new Vector3f(0,0,0), new Vector3f(dir.mul(4))));
					
					star.setColor(1.0f, 0.813437f, 0);
					star.setScale(0.2f);
					star.setState(STATES.SOLID_COLOR);
					star.setDraw(true);
					star.loadMesh();
					world.addMesh(star);
				}
			}
		}
		
		//win code
		if(platform == course.length - 2) {
			boolean winCondition = !star.draw() && (loop.getTime() < 90);
			if(winCondition) {
				//he won
				winningVariable = 1;
			}else if(star.draw()) {
				//did not get star
				winningVariable = 2;
			}else if(loop.getTime() > 90) {
				//not fast enough
				winningVariable = 3;
			}
			Launch.window.pause();
			//ui.UIInit.winScreen(winningVariable);
		}*/
		
		if (loop.getRMB() && loop.frames() % 4 == 0) {
			// remove all cubes
			for (int i = 0; i < world.getNumMeshes(); i++) {
				Mesh m = world.getMesh(i);
				if (m.getClass() != Sphere.class && m != floor) {
					world.removeMesh(m);
				}
			}
		}

		for (int i = 0; i < world.getNumMeshes(); i++) {
			Mesh m = world.getMesh(i);
			if (m.getClass() == Spring.class) {
				Spring s = (Spring) m;
				s.update();
			}
		}
		if (one != null && two != null) {
			Collider.CCD(one.getHull(), two.getHull());
		}

		
	}
	
	public int jumpValid() {
		for(int i = 0; i< course.length; i++) {
			Mesh thing = course[i];
			float raycast = Raycast.raycastAABB(new Vector3f(0,-1,0), cameraCollider.getHull().getPosition(), thing.getHull().getPosition(), new Vector3f(2,2,2));
			if(raycast != Float.MIN_VALUE && raycast < cameraCollider.getRadius()+0.25f) {
				return i;
			}
		}
		return -1;
	}
}
