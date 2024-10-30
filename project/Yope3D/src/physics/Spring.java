package physics;

import org.joml.Matrix4f;
import org.joml.Vector3f;

import visual.Launch;
import visual.Mesh;
import visual.Util;

public class Spring extends Mesh {
	private Mesh first;
	private Mesh second;
	private float k;
	private float restLength;

	public static Spring spring(Mesh first, Mesh second, float k, float restLength, int coils) {
		if (coils == 0) {
			// invis, spring is not to be displayed
			Spring s = new Spring(new float[1], new int[1], first, second, k, restLength);
			s.setDraw(false);
			return s;
		} else {
			Mesh m = Util.readObjFile("Assets\\Models\\taurus3.obj");
			float[] vertices = m.vertices();
			int[] ind = m.indices();
			for (int i = 0; i < vertices.length / 8; i++) {
				float angle = (float) Math.atan2(vertices[i * 8 + 2], -vertices[i * 8 + 1]);
				// increment is a function of angle
				vertices[i * 8] /= (10);
				vertices[i * 8 + 1] /= (10);
				vertices[i * 8 + 2] /= (10);
				vertices[i * 8] += (angle / (coils * 2.0f * 3.14159265359f));
			}
			float[] spring = new float[vertices.length * coils];
			int[] indices = new int[ind.length * coils];
			for (int i = 0; i < coils; i++) {
				for (int v = 0; v < vertices.length; v++) {
					if (v % 8 == 0) {
						spring[i * vertices.length + v] = (i * 1.0f / ((float) coils)) + vertices[v];
					} else {
						spring[i * vertices.length + v] = vertices[v];
					}
				}

				for (int index = 0; index < ind.length; index++) {
					indices[i * ind.length + index] = (i * vertices.length / 8) + ind[index];
				}
			}
			return new Spring(spring, indices, first, second, k, restLength);
		}
	}

	private Spring(float[] vertices, int[] indices, Mesh first, Mesh second, float k, float restLength) {
		super(vertices, indices);
		this.first = first;
		this.second = second;
		this.k = k;
		this.restLength = restLength;
	}

	@Override
	public Matrix4f getMM() {
		// spring forward is just difference in second and first
		Vector3f start = new Vector3f(first.getHull().getPosition());
		Vector3f forward = new Vector3f(first.getHull().getPosition()).sub(second.getHull().getPosition());

		// generate a arbitrary vector
		Vector3f rand = new Vector3f(0, 1, 0);
		// make sure no division by 0
		float epsilon = 0.01f;
		if (Math.abs(forward.x) < epsilon && Math.abs(forward.y - 1) < epsilon && Math.abs(forward.z) < epsilon) {
			rand = new Vector3f(1, 0, 0);
		}

		// cross with forward to get another direction
		Vector3f right = new Vector3f(forward).cross(rand);
		right.normalize();

		// cross forward with right to get up vector
		Vector3f up = new Vector3f(forward).cross(right);
		up.normalize();

		forward.mul(-1.0f);

		// now generate matrix
		Matrix4f modelMat = new Matrix4f();

		modelMat.m00(forward.x);
		modelMat.m01(forward.y);
		modelMat.m02(forward.z);
		modelMat.m03(0.0f);

		modelMat.m10(right.x);
		modelMat.m11(right.y);
		modelMat.m12(right.z);
		modelMat.m13(0.0f);

		modelMat.m20(up.x);
		modelMat.m21(up.y);
		modelMat.m22(up.z);
		modelMat.m23(0.0f);

		modelMat.m30(start.x);
		modelMat.m31(start.y);
		modelMat.m32(start.z);
		modelMat.m33(1.0f);

		return modelMat;
	}

	public void update() {
		// calculate spring force
		Vector3f deltaX = new Vector3f(first.getHull().getPosition()).sub(second.getHull().getPosition());
		float length = deltaX.length();
		deltaX.normalize();
		deltaX.mul(length - restLength);
		deltaX.mul(k);

		// calc acceleration
		float dt = Launch.world.getDT();
		Vector3f acceleration1 = new Vector3f(deltaX).mul(-dt / first.getHull().getMass());
		Vector3f acceleration2 = new Vector3f(deltaX).mul(dt / second.getHull().getMass());

		// apply
		first.getHull().addVelocity(acceleration1);
		second.getHull().addVelocity(acceleration2);

		// damp
		first.getHull().addVelocity(new Vector3f(first.getHull().getVelocity()).mul(-0.0075f));
		second.getHull().addVelocity(new Vector3f(second.getHull().getVelocity()).mul(-0.0075f));
	}

	public Mesh getFirst() {
		return first;
	}

	public Mesh getSecond() {
		return second;
	}

	public float getK() {
		return k;
	}
}
