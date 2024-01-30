package scripts;

import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Vector4f;
import org.lwjgl.glfw.GLFW;

import physics.Barrier;
import physics.Sphere;
import visual.Mesh;
import visual.Util;
import visual.Util.STATES;

public class Platformer extends Script {
	Mesh floor;
	Sphere cameraCollider;

	@Override
	public void init() {
		super.init();

		// add the bounding box
		Barrier[] barriers = { new Barrier(new Vector3f(0, 1, 0), new Vector3f(0, 1, 0)),
				new Barrier(new Vector3f(0, -1, 0), new Vector3f(0, 50, 0)),
				new Barrier(new Vector3f(1, 0, 0), new Vector3f(-25, 0, 0)),
				new Barrier(new Vector3f(-1, 0, 0), new Vector3f(25, 0, 0)),
				new Barrier(new Vector3f(0, 0, 1), new Vector3f(0, 0, -25)),
				new Barrier(new Vector3f(0, 0, -1), new Vector3f(0, 0, 25)), };

		for (Barrier b : barriers) {
			world.addBarrier(b);
		}

		cameraCollider = Sphere.genSphere(1,1f);
		cameraCollider.setPosition(new Vector3f(0, 1, 0));
		cameraCollider.setColor(1.0f, 0.0f, 1.0f);
		cameraCollider.setState(STATES.SOLID_COLOR);
		cameraCollider.setDraw(false);
		world.addMesh(cameraCollider);

		floor = Util.readObjFile("Assets\\Models\\floor.obj");
		floor.setPosition(new Vector3f(0, -0.5f, 0));

		floor.setTexture("Assets\\Textures\\wood.jpg");
		floor.setState(STATES.TEXTURED);
		floor.calcExtent(STATES.FLOOR_MASS);

		floor.loadMesh();
		world.addMesh(floor);

		loop.getCamera().setMoveSpeed(1f);
		loop.getCamera().setPosition(new Vector3f(0, 1, 0));

		world.setLight(new Vector3f(0, 100, 0));

		world.setCollisions(1);
		
		world.setDT(1.0f / 60.0f);
	}

	@Override
	public void update() {
		if (loop.getKey(GLFW.GLFW_KEY_W) || loop.getKey(GLFW.GLFW_KEY_A) || loop.getKey(GLFW.GLFW_KEY_S)
				|| loop.getKey(GLFW.GLFW_KEY_D) || loop.getKey(GLFW.GLFW_KEY_SPACE)
				|| loop.getKey(GLFW.GLFW_KEY_LEFT_SHIFT)) {

			if (loop.getKey(GLFW.GLFW_KEY_SPACE)) {
				cameraCollider.addVelocity(new Vector3f(0, loop.getCamera().getMoveSpeed() * 10, 0));
			}
			if (loop.getKey(GLFW.GLFW_KEY_LEFT_SHIFT)) {
				cameraCollider.addVelocity(new Vector3f(0, -loop.getCamera().getMoveSpeed(), 0));
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

			push.mul(loop.getCamera().getMoveSpeed());
			
			cameraCollider.addVelocity(push);
		}
		
		//frictional decay
		cameraCollider.getVelocity().x *= 0.9f;
		cameraCollider.getVelocity().z *= 0.9f;
		
		
		loop.getCamera().setPosition(cameraCollider.getPosition());
		
		loop.getCamera().update();

		loop.getCamera().sendState();
		
		
		
		if(loop.getRMB() && loop.frames() % 4 == 0) {
			//remove all cubes	
			for(int i= 0; i< world.getNumMeshes(); i++) {
				Mesh m = world.getMesh(i);
				if(m.getClass() != Sphere.class && m != floor) {
					world.removeMesh(m);
				}
			}
		}

		renderer.compute(world, 1);
		
		if(loop.getLMB() && loop.frames() % 4 == 0) {
			//add a cube
			Mesh cube = Util.readObjFile("Assets\\Models\\cube.obj");
			cube.setTexture("Assets\\Textures\\metal_texture.jpg");
			cube.setState(STATES.TEXTURED);
			//cube.setScale(10.0f);
			
			//get forward vector
			Matrix4f invCam = loop.getCamera().genViewMatrix().transpose();
			Vector4f tempForward = (new Vector4f(0,0,-1,1)).mul(invCam);
			Vector3f forward = new Vector3f(tempForward.x, tempForward.y, tempForward.z);
			
			//set it to be 10 units infront of cam
			forward.normalize(10.0f);
			
			//get random orientation
			float xAngle = (float) (Math.random() * 2 * 3.14159265359);
			float yAngle = (float) (Math.random() * 2 * 3.14159265359);
			float zAngle = (float) (Math.random() * 2 * 3.14159265359);
			
			//set as cube position
			cube.setPosition(new Vector3f(cameraCollider.getPosition()).add(forward));
			//set cube rotation
			cube.setRotation(new Vector3f(xAngle,0,zAngle));
			
			cube.loadMesh();
			cube.calcExtent(1.0f);
			world.addMesh(cube);
		}
	}

}
