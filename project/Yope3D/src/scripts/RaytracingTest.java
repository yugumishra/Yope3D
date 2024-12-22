package scripts;

import org.joml.Math;
import org.joml.Matrix3f;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Vector4f;
import org.lwjgl.glfw.GLFW;

import audio.Listener;
import physics.BoundedBarrier;
import physics.CSphere;
import physics.Sphere;
import visual.FlashLight;
import visual.Launch;
import visual.Mesh;
import visual.PointLight;

public class RaytracingTest extends Script{
	
	Mesh cameraCollider;
	
	@Override
	public void init() {
		super.init();
		
		world.addBarrier(new BoundedBarrier(55, 55, new Vector3f(0, 1,0), new Vector3f(1,0,0), new Vector3f(0,0,0)));
		
		//add the camera collider
		cameraCollider = Sphere.genSphere(0, 0.5f);
		cameraCollider.redefineHull(new CSphere(1.0f, 0.5f, new Vector3f(0, 2, 25), new Vector3f(0,0,-0.1f), new Vector3f(), new Vector3f()));
		cameraCollider.setDraw(false);
		world.addMesh(cameraCollider);
		
		loop.getCamera().setPosition(cameraCollider.getHull().getPosition());
		loop.getCamera().setMoveSpeed(1f);
		
		
		//set the world time step
		world.setDT(1.0f/100.0f);
		
		world.addLight(new PointLight(new Vector3f( 40,50,-40), new Vector3f(1,0,1), new Vector3f(100000000.0f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f( 40,50, 40), new Vector3f(1,1,1), new Vector3f(100000000.0f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f(-40,50, 40), new Vector3f(1,1,1), new Vector3f(100000000.0f,0.01f, 0.00001f)));
		world.addLight(new PointLight(new Vector3f(-40,50, -40), new Vector3f(1,1,1), new Vector3f(100000000.0f,0.01f, 0.00001f)));
		
		world.addLight(new FlashLight(0.99f, 0.8f, new Vector3f(1, 1, 1), new Vector3f(1f, 0.01f, 0.00001f)));
		
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
		
		Vector3f forward = Launch.window.getCamera().genViewMatrix().get3x3(new Matrix3f()).transpose().transform(new Vector3f(0,0,-1));
		
		FlashLight light = (FlashLight) world.getLight(4);
		light.setDirection(new Vector3f(forward.x, forward.y, forward.z));
		world.lightChanged();
		
		if(loop.getRMB() && loop.frames() % 8 == 0) {
			for(int i = 0; i< 4; i++) {
				Vector3f lightCharacteristics = world.getLight(i).getLightCharacteristics();
				float magn = lightCharacteristics.x;
				if(magn > 10000000) {
					magn = 5.0f;
				}else if(Math.abs(magn - 5.0f) < 0.01f) {
					magn = 100000000.0f;
				}
				world.getLight(i).setLightCharacteristics(new Vector3f(magn, lightCharacteristics.y, lightCharacteristics.z));
			}
		}
		
		if(loop.getBackwardMB() && loop.frames() % 8 == 0) {
			Vector3f lightCharacteristics = world.getLight(4).getLightCharacteristics();
			float magn = lightCharacteristics.x;
			if(magn > 10000000) {
				magn = 1.0f;
			}else if(Math.abs(magn - 1.0f) < 0.01f) {
				magn = 100000000.0f;
			}
			world.getLight(4).setLightCharacteristics(new Vector3f(magn, lightCharacteristics.y, lightCharacteristics.z));
		}
		
		//update cam and send state
		loop.getCamera().setPosition(cameraCollider.getHull().getPosition());
		loop.getCamera().update();
		loop.getCamera().sendState();
	}
}
