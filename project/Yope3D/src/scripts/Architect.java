package scripts;

import java.util.ArrayList;

import org.joml.Math;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Vector4f;
import org.lwjgl.glfw.GLFW;

import audio.Listener;
import audio.Source;
import physics.BarrierHull;
import physics.BoundedBarrier;
import physics.CSphere;
import physics.Hull;
import physics.Raycast;
import physics.Sphere;
import visual.Launch;
import visual.Mesh;
import visual.PointLight;
import visual.SpotLight;
import visual.Util;
import visual.Util.STATES;

public class Architect extends Script{
	
	Mesh cameraCollider;
	Mesh tracker;
	boolean acceptingInputs;
	boolean flashlightOn;
	
	//audio source
	Source source;
	Source nearby;
	Source lightOn;
	Source lightOff;
	
	Source ambient;
	
	int clickFrame;
	int clickFrame2;
	
	@Override
	public void init() {
		super.init();
		flashlightOn = false;
		
		//add a floor
		Mesh floor = Util.readObjFile("Assets\\Models\\plane.obj");
		floor.setState(STATES.TEXTURED);
		floor.setTexture("Assets\\Textures\\brick.jpg");
		floor.getHull().fix();
		floor.loadMesh();
		world.addMesh(floor);
		
		world.instantiateCollisionTree(new Vector3f(-100, 0, -100), new Vector3f(100, 100, 100), 3);
		
		//add a bunch of cube
		Mesh cube = Mesh.cube();
		cube.setState(STATES.SOLID_COLOR);
		cube.setColor(0.2f, 0.8f, 0.9f);
		cube.getHull().setPosition(new Vector3f(25,5,0));
		cube.getHull().addVelocity(new Vector3f(-8,0,0));
		//cube.getHull().setPosition(new Vector3f((float) Math.random() * 100 - 50, (float) Math.random() * 25, (float) Math.random() * 100 - 50));
		cube.loadMesh();
		world.addMesh(cube);
		
		Mesh cube2 = Mesh.cube();
		cube2.setState(STATES.SOLID_COLOR);
		cube2.setColor(0.8f, 0.2f, 0.9f);
		cube2.getHull().setPosition(new Vector3f(-25,5,0));
		cube2.getHull().addVelocity(new Vector3f(8,0,0));
		//cube.getHull().setPosition(new Vector3f((float) Math.random() * 100 - 50, (float) Math.random() * 25, (float) Math.random() * 100 - 50));
		cube2.loadMesh();
		world.addMesh(cube2);
		
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
		
		//init the accepting input
		acceptingInputs = true;
		
		//set the world time step
		world.setDT(1.0f/60.0f);
		
		//set the light position
		world.addLight(new SpotLight(new Vector3f(0, 10, 0), new Vector3f(1,0,0), 0.99f, 0.219439963211f, new Vector3f(1,1,1), new Vector3f(0,0.1f,0.01f)));
		for(int i =0; i< 0; i++) {
			float x = -50 + 25 *i;
			float y = 20;
			float z = 0;
			
			world.addLight(new SpotLight(new Vector3f(x,y,z), new Vector3f(0,-y,0).normalize(), 0.99f, 0.21943996211f, new Vector3f((float) Math.random(), (float) Math.random(), (float) Math.random()), new Vector3f(1f, 0.01f, 0.001f)));
		}
		
		world.addLight(new PointLight(new Vector3f( 40,10,-40), new Vector3f(1,1,1), new Vector3f(0.5f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f( 40,10, 40), new Vector3f(1,1,1), new Vector3f(0.5f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f(-40,10, 40), new Vector3f(1,1,1), new Vector3f(0.5f,0.01f, 0.00001f)));
		
		Launch.world.lightChanged();
		
		//create a sound source
		source = new Source(Util.readOggFile("Assets\\Sounds\\mono\\fnaf4-foxy-closet-jumpscare.ogg"), new Vector3f(0, 1, -45), new Vector3f(0,0,0));
		source.setGain(1.0f);
		source.setPitch(1.0f);
		Source ambient = new Source(Util.readOggFile("Assets\\Sounds\\stereo\\glitchy-static.ogg"), new Vector3f(0,0,0), new Vector3f(0,0,0));
		ambient.setGain(0.01f);
		ambient.enableLooping();
		ambient.play();
		nearby = new Source(Util.readOggFile("Assets\\Sounds\\mono\\growling-ambience.ogg"),new Vector3f(0,0,0), new Vector3f(0,0,0));
		nearby.setGain(1f);
		lightOn = new Source(Util.readOggFile("Assets\\Sounds\\stereo\\flashlight-click-on.ogg"), new Vector3f(0,0,0), new Vector3f(0,0,0));
		lightOn.setGain(1.0f);
		lightOff =new Source(Util.readOggFile("Assets\\Sounds\\stereo\\flashlight-click-off.ogg"),new Vector3f(0,0,0), new Vector3f(0,0,0));
		lightOff.setGain(1.0f);
		//source.enableLooping();
		//source.play();
		Launch.window.am.addSource(source);
		Launch.window.am.addSource(nearby);
		
		//init listener
		Listener.init(cameraCollider.getHull().getPosition(), new Vector3f(), new Vector3f(0,0,-1), new Vector3f(0,1,0), 1.0f);
	}
	@Override
	public void update() {
		//input
		if(acceptingInputs && (loop.getKey(GLFW.GLFW_KEY_W) || loop.getKey(GLFW.GLFW_KEY_A) || loop.getKey(GLFW.GLFW_KEY_S) || loop.getKey(GLFW.GLFW_KEY_D) || loop.getKey(GLFW.GLFW_KEY_SPACE) || loop.getKey(GLFW.GLFW_KEY_LEFT_CONTROL))) {
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
			if (loop.getKey(GLFW.GLFW_KEY_SPACE) && (cameraCollider.getHull().getPosition().y < 0.75f || jumpValid())) {
				cameraCollider.getHull().addVelocity(new Vector3f(0, 2, 0));
			}
			if (loop.getKey(GLFW.GLFW_KEY_LEFT_CONTROL)) {
				cameraCollider.getHull().addVelocity(new Vector3f(0,-1, 0));
			}
			
			//use the inverse camera to get into world space
			Matrix4f inv = loop.getCamera().genViewMatrix().transpose();
			moveDir.mul(inv);
			Vector3f forwardRay = new Vector3f(moveDir.x, moveDir.y, moveDir.z);
			
			Vector3f up = new Vector3f(0, 1, 0);
			forwardRay.sub(up.mul(forwardRay.dot(up)));
			
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
		
		if(loop.getForwardMB() && loop.frames() % 4 == 0) {
			source.setGain(source.getGain() + 0.05f);
		}
		if(loop.getBackwardMB() && loop.frames() % 4 == 0) {
			source.setGain(source.getGain() - 0.05f);
		}
		
		
		Matrix4f inv = loop.getCamera().genViewMatrix().transpose();
		
		
		SpotLight light = (SpotLight) world.getLight(0);
		Vector3f lightCharacteristics = light.getLightCharacteristics();
		Vector4f forward = new Vector4f(0,0,-1,1).mul(inv);
		
		
		if(loop.getLMB() && loop.frames() - clickFrame > 40) {
			flashlightOn = !flashlightOn;
			if(flashlightOn) {
				lightOn.play();
			}else {
				lightCharacteristics.x = 0;
				Launch.world.lightChanged();
				lightOff.play();
			}
			clickFrame = loop.frames();
			
		}
		
		
		if(flashlightOn) {
			//track the player
			lightCharacteristics.x = 1;
			light.setPosition(new Vector3f(cameraCollider.getHull().getPosition()));
			
			light.setDirection(new Vector3f(forward.x, forward.y, forward.z));
			Launch.world.lightChanged();
			
			
		}
		/*
		Vector3f diff = new Vector3f(cameraCollider.getHull().getPosition()).sub(tracker.getHull().getPosition());
		if(flashlightOn || diff.length() < 15) {
			//have the tracker track
			if(diff.length() < 3.5f && !source.isPlaying()) {
				source.play();
			}
			
			diff.normalize();
			tracker.getHull().addVelocity(diff);
			//update everything
			source.setPosition(tracker.getHull().getPosition());
			source.setVelocity(tracker.getHull().getVelocity());
			Vector3f velocity = tracker.getHull().getVelocity();
			velocity.x *= 0.9f;
			velocity.z *= 0.9f;
			tracker.getHull().setVelocity(velocity);
		}
		
		if(diff.length() < 35f && !nearby.isPlaying()) {
			nearby.play();
		}else if(diff.length() > 35) {
			nearby.pause();
		}
		
		nearby.setPosition(tracker.getHull().getPosition());
		nearby.setVelocity(tracker.getHull().getVelocity());*/
		
		//update cam and send state
		loop.getCamera().setPosition(cameraCollider.getHull().getPosition());
		loop.getCamera().update();
		loop.getCamera().sendState();
		
		//update listener
		Vector4f up = new Vector4f(0,1,0,1).mul(inv);
		Listener.setOrientation(new Vector3f(forward.x, forward.y, forward.z), new Vector3f(up.x, up.y, up.z));
		Listener.setPosition(cameraCollider.getHull().getPosition());
		Listener.setVelocity(cameraCollider.getHull().getVelocity());
		
		
		
		//do death
		if(cameraCollider.getHull().getPosition().y < -50) {
			//fell off the map
			ui.UIInit.deathScreen(0);
			Launch.window.pause();
		}
	}
	
	private boolean jumpValid() {
		//iterate through the list of possible barrier hulls 
		//given by collision tree
		ArrayList<Hull> possibles = world.getObjects(cameraCollider.getHull());
		for(Hull h: possibles) {
			BarrierHull box = (BarrierHull) h;
			
			float k = Raycast.raycastAABB(new Vector3f(0,-1,0), cameraCollider.getHull().getPosition(), box.getPosition(), box.getExtent());
			if(k < 0.75f && k != Float.MIN_VALUE) {
				//we got it
				return true;
			}
		}
		return false;
	}
	
	@Override
	public void scrolled(double xOffset, double yOffset) {
		source.setPitch(source.getPitch() + (float) yOffset * 0.1f);
	}
	
}
