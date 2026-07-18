package scripts;

import org.joml.Math;
import org.joml.Quaternionf;
import org.joml.Vector3f;
import org.lwjgl.glfw.GLFW;

import physics.AABB;
import physics.Hull;
import physics.OBB;
import physics.Sphere;
import visual.Launch;
import visual.Mesh;
import visual.PointLight;
import visual.Util;
import visual.Util.STATES;

public class Tester extends Script{
	
	Hull cameraCollider;
	Mesh cube2;
	
	public Vector3f[] incFace;
	Mesh[] debugSpheres;
	
	@Override
	public void init() {
		super.init();
		
		//add a floor
		Mesh floor = Mesh.box(new Vector3f(50, 0.5f, 50));
		floor.setState(STATES.SOLID_COLOR);
		floor.setColor(1.0f, 0.0f,0.0f);
		floor.redefineHull(new AABB(new Vector3f(0, 0, 0), new Vector3f(50, 1f, 50)));
		floor.getHull().fix();
		floor.loadMesh();
		world.addMesh(floor);
		
		Mesh dummy1 = new Mesh(null, null);
		dummy1.setDraw(false);
		dummy1.redefineHull(new AABB(new Vector3f(50, 50, 0), new Vector3f(1f, 50, 50)));
		world.addMesh(dummy1);
		
		Mesh dummy2 = new Mesh(null, null);
		dummy2.setDraw(false);
		dummy2.redefineHull(new AABB(new Vector3f(-50, 50, 0), new Vector3f(1f, 50, 50)));
		world.addMesh(dummy2);
		
		Mesh dummy3 = new Mesh(null, null);
		dummy3.setDraw(false);
		dummy3.redefineHull(new AABB(new Vector3f(0, 50, 50), new Vector3f(50, 50, 1f)));
		world.addMesh(dummy3);
		
		Mesh dummy4 = new Mesh(null, null);
		dummy4.setDraw(false);
		dummy4.redefineHull(new AABB(new Vector3f(0, 50, -50), new Vector3f(50, 50, 1f)));
		world.addMesh(dummy4);
		
		
		//add a bunch of cube
		/*
		Mesh cube = Mesh.cube();
		cube.setState(STATES.SOLID_COLOR);
		cube.setColor(0.2f, 0.8f, 0.9f);
		cube.redefineHull(new AABB(new Vector3f(0,15,0), new Vector3f(0,0,0), 1.0f));
		//cube.getHull().setPosition(new Vector3f((float) Math.random() * 100 - 50, (float) Math.random() * 25, (float) Math.random() * 100 - 50));
		cube.loadMesh();
		world.addMesh(cube);
		*/
		cube2 = Mesh.box(new Vector3f(1, 1, 1));
		cube2.setState(STATES.SOLID_COLOR);
		cube2.setColor(0.8f, 0.2f, 0.9f);
		cube2.redefineHull(new OBB(new Vector3f(0,8,0), new Vector3f(0,0,0), 10f, new Vector3f(1,1,1)));
		Quaternionf q = Util.xyzRotToQuat(new Vector3f(0,2*3.1415926535f * ((float) Math.random()), 0*2*3.1415926535f * ((float) Math.random())));
		cube2.getHull().setRotation(q);
		//cube.getHull().setPosition(new Vector3f((float) Math.random() * 100 - 50, (float) Math.random() * 25, (float) Math.random() * 100 - 50));
		cube2.loadMesh();
		world.addMesh(cube2);
		
		
		//add the camera collider
		
		cameraCollider = new Sphere(new Vector3f(0, 2, 5), new Vector3f(0,0,0), 1.1f, 1.0f);
		Mesh dummy = new Mesh(null, null);
		dummy.setDraw(false);
		dummy.redefineHull(cameraCollider);
		world.addMesh(dummy);
		
		debugSpheres = new Mesh[4];
		incFace = new Vector3f[4];
		
		for(int i = 0; i < 4; i++) {
			incFace[i] = new Vector3f(10000, 10000, 1000);
			
			Mesh debug = Mesh.genSphere(3, 0.15f);
			debug.setDraw(true);
			debug.setState(STATES.SOLID_COLOR);
			debug.setColor(0.0f, 1.0f, 0.0f);
			debug.redefineHull(new Sphere(incFace[i], new Vector3f(0,0,0), 1.0f, 0.05f));
			debug.getHull().tangible = false;
			world.addMesh(debug);
			debugSpheres[i] = debug;
		}
		
		/*
		Mesh sphere = Mesh.genSphere(2, 5.0f);
		sphere.setState(STATES.SOLID_COLOR);
		sphere.setColor(1,1,1);
		sphere.redefineHull(new Sphere(new Vector3f(0, 6, 0), new Vector3f(), 1.0f, new Vector3f(), new Vector3f(), 5.0f));
		//sphere.getHull().fix();
		sphere.loadMesh();
		world.addMesh(sphere);
		*/
		
		loop.getCamera().setPosition(cameraCollider.getPosition());
		loop.getCamera().setMoveSpeed(1f);
		
		//set the world time step
		world.setDT(1.0f/100.0f);
		
		//set the light position
		world.addLight(new PointLight(new Vector3f(-40,10,-40), new Vector3f(1,1,1), new Vector3f(0.25f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f( 40,10,-40), new Vector3f(1,1,1), new Vector3f(0.25f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f( 40,10, 40), new Vector3f(1,1,1), new Vector3f(0.25f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f(-40,10, 40), new Vector3f(1,1,1), new Vector3f(0.25f,0.01f, 0.00001f)));
		
		Launch.world.lightChanged();
	}
	@Override
	public void update() {
		//input
		if (loop.getKey(GLFW.GLFW_KEY_W) || loop.getKey(GLFW.GLFW_KEY_A) || 
			    loop.getKey(GLFW.GLFW_KEY_S) || loop.getKey(GLFW.GLFW_KEY_D) || 
			    loop.getKey(GLFW.GLFW_KEY_SPACE) || loop.getKey(GLFW.GLFW_KEY_LEFT_CONTROL)) {

			    //get movement direction
			    Vector3f moveDir = new Vector3f();

			    if (loop.getKey(GLFW.GLFW_KEY_W)) moveDir.z -= 1;
			    if (loop.getKey(GLFW.GLFW_KEY_S)) moveDir.z += 1;
			    if (loop.getKey(GLFW.GLFW_KEY_A)) moveDir.x -= 1;
			    if (loop.getKey(GLFW.GLFW_KEY_D)) moveDir.x += 1;

			    //transform to world space using the camera view
			    moveDir.rotateY(loop.getCamera().getRotation().y);

			    //prevent vertical movement from affecting forward direction
			    moveDir.y = 0;

			    //apply movement speed
			    moveDir.mul(loop.getCamera().getMoveSpeed());

			    //sprinting
			    if (loop.getKey(GLFW.GLFW_KEY_LEFT_SHIFT)) {
			        moveDir.mul(2.0f);
			    }

			    //apply vertical velocity
			    if (loop.getKey(GLFW.GLFW_KEY_SPACE)) {
			        moveDir.y += 1.0f;
			    }
			    if (loop.getKey(GLFW.GLFW_KEY_LEFT_CONTROL)) {
			        moveDir.y -= 1.0f;
			    }

			    //add velocity to the camera collider
			    moveDir.x *= 0.9f;
			    moveDir.z *= 0.9f;
			    cameraCollider.addVelocity(moveDir);
			}

		//add some friction to camera velocity
		Vector3f vel = cameraCollider.getVelocity();
		cameraCollider.setVelocity(vel);
		
		//cap camera rotation
		Vector3f cameraRot = new Vector3f(loop.getCamera().getRotation());
		cameraRot.x = Math.min(cameraRot.x,  3.1415926535f/2);
		cameraRot.x = Math.max(cameraRot.x, -3.1415926535f/2);
		loop.getCamera().setRotation(cameraRot);
		
		for(int i = 0; i< 4; i++) {
			incFace[i] = new Vector3f(10000, 100000, 10000);
		}
		//advance world
		world.advance();
		
		for(int i = 0; i< 4; i++) {
			debugSpheres[i].getHull().setPosition(incFace[i]);
		}
		
		loop.getCamera().setPosition(cameraCollider.getPosition());
		loop.getCamera().update();
		loop.getCamera().sendState();
		
		if(loop.getForwardMB() && loop.frames() % 4 == 0) {
			for(int i = 0; i< 25; i++) {
				Mesh sphere = Mesh.genSphere(1, 1.0f);
				sphere.setState(STATES.SOLID_COLOR);
				sphere.setColor((float) Math.random(), (float) Math.random(), (float) Math.random());
				
				Vector3f forwardRay = new Vector3f((float) Math.random() - 0.5f, (float) Math.random()-0.5f, (float) Math.random()-0.5f);
				
				sphere.redefineHull(new Sphere(cameraCollider.getPosition().add(forwardRay.mul(4)), new Vector3f(forwardRay.mul(10)), 1.0f, new Quaternionf().identity(), new Vector3f()));
				sphere.loadMesh();
				
				world.addMesh(sphere);
			}
		}
		
		if(loop.getLMB() && loop.frames() % 4 == 0) {
			cube2.getHull().setPosition(new Vector3f(0,2.8f,0));
			cube2.getHull().setVelocity(new Vector3f());
			cube2.getHull().setOmega(new Vector3f());
			Quaternionf quat = Util.xyzRotToQuat(new Vector3f(0,0,3.1415926535f * ((float) Math.random())));
			cube2.getHull().setRotation(quat);
		}
	}
	
}

