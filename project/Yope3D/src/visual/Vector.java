package visual;

import org.joml.Matrix4f;
import org.joml.Vector3f;

public class Vector extends Mesh{
	Vector3f up;
	Vector3f forward;
	Vector3f right;

	private Vector(float[] vertices, int[] indices) {
		super(vertices, indices);
	}
	
	@Override
	public Matrix4f getMM() {
		Matrix4f transform = new Matrix4f();
		transform.translate(super.getHull().getPosition());
		transform.m00(up.x);
		transform.m01(up.y);
		transform.m02(up.z);
		
		transform.m10(forward.x);
		transform.m11(forward.y);
		transform.m12(forward.z);
		
		transform.m20(right.x);
		transform.m21(right.y);
		transform.m22(right.z);
		
		return transform;
		
	}
	
	public static Vector genVector(Vector3f forward) {
		Mesh v = Util.readObjFile("Assets\\Models\\vector.obj");
		
		Vector3f up = null;
		if((forward.x == 0 && forward.z == 0 && forward.y == 1) == false) {
			up = new Vector3f(0,1,0);
		}else {
			up = new Vector3f(1,0,0);
		}
		Vector3f right = null;
		right = new Vector3f(forward).cross(up);
		right.normalize();
		up = new Vector3f(right).cross(forward);
		up.normalize();
		
		Vector vector = new Vector(v.vertices(), v.indices());
		
		vector.up = up;
		vector.right = right;
		vector.forward = forward;
		
		return vector;
	}
}
