package scripts;

import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Vector4f;
import org.lwjgl.glfw.GLFW;

import physics.Barrier;
import physics.Sphere;
import visual.Launch;
import visual.Mesh;
import visual.PointLight;
import visual.Util.STATES;

public class Shooter extends Script {
	Sphere star;
	Vector3f pos;
	boolean set = true;
	float mult = 1500000f;
	float val = 300.0f;

	public void init() {
		super.init();
		
		
		//add the bounding box
		Barrier[] barriers = {
				new Barrier(new Vector3f(0,1,0), new Vector3f(0,0,0)),
				new Barrier(new Vector3f(0,-1,0), new Vector3f(0,10000,0)),
				new Barrier(new Vector3f(1,0,0), new Vector3f(-5000,0,0)),
				new Barrier(new Vector3f(-1,0,0), new Vector3f(5000,0,0)),
				new Barrier(new Vector3f(0,0,1), new Vector3f(0,0,-5000)),
				new Barrier(new Vector3f(0,0,-1), new Vector3f(0,0,5000)),
		};
		
		for(Barrier b: barriers) {
			world.addBarrier(b);
		}
		

		star = Sphere.genSphere(3, 45.0f);
		star.setMass(Float.MAX_VALUE);

		star.setColor(1, 1, 1);
		star.setDraw(true);

		star.getHull().setPosition(new Vector3f(0, 5000, 0));
		star.setState(STATES.LIGHT);
		star.setColor(1.0f, 1.0f, 1.0f);
		pos = star.getHull().getPosition();

		world.addMesh(star);

		loop.getCamera().setMoveSpeed(20.0f);
		loop.getCamera().setPosition(new Vector3f(0, 5000, 500));

		world.setCollisions(0);
	}

	public void update() {
		if (loop.getKey(GLFW.GLFW_KEY_W) || loop.getKey(GLFW.GLFW_KEY_A) || loop.getKey(GLFW.GLFW_KEY_S)
				|| loop.getKey(GLFW.GLFW_KEY_D) || loop.getKey(GLFW.GLFW_KEY_SPACE)
				|| loop.getKey(GLFW.GLFW_KEY_LEFT_SHIFT)) {

			if (loop.getKey(GLFW.GLFW_KEY_SPACE)) {
				loop.getCamera().addVelocity(new Vector3f(0, loop.getCamera().getMoveSpeed(), 0));
			}
			if (loop.getKey(GLFW.GLFW_KEY_LEFT_SHIFT)) {
				loop.getCamera().addVelocity(new Vector3f(0, -loop.getCamera().getMoveSpeed(), 0));
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
			loop.getCamera().addVelocity(push);
		}

		pos = star.getHull().getPosition();

		loop.getCamera().update();

		loop.getCamera().sendState();

		if (set) {
			world.advance();

			if (loop.getKey(GLFW.GLFW_KEY_E) && loop.frames() % 4 == 0) {
				for (int i = 0; i < 25; i++) {
					// we need to fire a sphere
					float theta = (float) Math.random();
					theta *= 2;
					theta *= (float) Math.PI;

					float phi = (float) Math.random();
					phi *= 2;
					phi *= (float) Math.PI;

					float nar = (float) Math.cos(phi);
					float y = (float) Math.sin(phi);

					float x = (float) Math.cos(theta) * nar;
					float z = (float) Math.sin(theta) * nar;

					Vector4f direction = new Vector4f(x, y, z, 1.0f);

					// take the transpose of the camera transformation matrix to cast it back into
					// world space
					Matrix4f viewMat = loop.getCamera().genViewMatrix();
					viewMat.transpose();

					// then apply the transpose transformation to the direction ray
					direction.mul(viewMat);

					Vector3f dir = new Vector3f(direction.x, direction.y, direction.z);
					dir.normalize();

					// shoot the sphere in this direction
					Sphere s = Sphere.genSphere(2, 10.0f);

					s.setMass(0.00000000000000000000001f);

					float r = (float) Math.random();
					float g = (float) Math.random();
					float b = (float) Math.random();

					s.setColor(r, g, b);

					Vector3f position = star.getHull().getPosition();
					position.add(dir.mul(star.getRadius() + s.getRadius() + 0.1f));
					s.getHull().setPosition(position);
					s.getHull().setVelocity(dir.mul(5));
					s.setState(STATES.SOLID_COLOR);

					s.loadMesh();

					world.addMesh(s);
				}
			} else if (loop.getKey(GLFW.GLFW_KEY_Q)) {
				int num = world.getNumMeshes() / 2;
				for (int i = 0; i < num; i++) {
					Mesh m = world.getMesh(i);
					if (m.getClass() == Sphere.class && m != star) {
						world.removeMesh(i);
					}
				}
			}

			if (loop.getLMB() || loop.getRMB()) {
				// this world shall know pain
				for (int i = 0; i < world.getNumMeshes(); i++) {
					Mesh m = world.getMesh(i);
					if (m.getClass() == Sphere.class && m != star) {
						// we can apply the pain

						Vector3f diff = star.getHull().getPosition().sub(m.getHull().getPosition());

						float multiplier = diff.length();
						diff.normalize();
						if (multiplier > 0.01f) {
							multiplier = mult / (multiplier * multiplier);
						}
						multiplier *= (loop.getRMB()) ? (-1) : (1);
						m.getHull().addImpulse(diff.mul(multiplier * m.getHull().getMass()));
					}
				}
			}
		}

		Launch.world.addLight(new PointLight(pos, new Vector3f(1,1,1), new Vector3f(1,0,0)));
	}

	@Override
	public void scrolled(double xOffset, double yOffset) {
		float multiplier = (float) Math.pow(1.1, yOffset);
		mult *= multiplier;
	}
}
