package physics;

import org.joml.Vector3f;

//a collection of math methods relating to ray casts
//they cast a ray and check for intersections with various collision geometries
public class Raycast {

	public static float raycastSphere(Vector3f ray, Vector3f start, Sphere sphere) {
		return raycastSphere(ray, start, sphere.getHull().getPosition(), sphere.getRadius());
	}
	
	public static float raycastSphere(Vector3f ray, Vector3f start, Vector3f sphereCenter, float sphereRadius) {
		float raySquared = ray.dot(ray);
		Vector3f diff = new Vector3f(start).sub(sphereCenter);
		
		float rayDiff = ray.dot(diff);
		float diffSquared = diff.dot(diff);
		
		float det = rayDiff * rayDiff - diffSquared + sphereRadius * sphereRadius;
		if(det < 0) {
			return -1.0f;
		}
		det = (float) Math.sqrt(det);
		float t1 = -rayDiff - det;
		float t2 = -rayDiff + det;
		
		t1 /= raySquared;
		t2 /= raySquared;
		
		//return first hit
		if(t1 < t2) {
			return t1;
		}else {
			return t2;
		}
	}
	
	public static float raycastAABB(Vector3f ray, Vector3f start, Vector3f aabbPos, Vector3f extent) {
		//we need to find the solution to this vector equation:
		//(start + k*ray).unitAxis = (aabbPos + extent).unitAxis
		//its a 3d eval_dir method (read ColliderCCD.java)
		Vector3f diff = new Vector3f(start).sub(aabbPos);
		float xK = Float.MIN_VALUE, yK = Float.MIN_VALUE, zK = Float.MIN_VALUE;
		//if the ray is not moving in the x dir, no need to run the x-check
		if(ray.x != 0) {
			//these 2 ks are the intersections for the x planes
			float k1 = (-diff.x+extent.x)/(ray.x);
			float k2 = (-diff.x-extent.x)/(ray.x);
			//we need to verify that these ks lie in the aabb and not off of it
			Vector3f suspectedHit1 = new Vector3f(start).add(new Vector3f(ray).mul(k1));
			Vector3f suspectedHit2 = new Vector3f(start).add(new Vector3f(ray).mul(k2));
			//check the ys and zs
			boolean k1Valid = !(suspectedHit1.y > aabbPos.y + extent.y || suspectedHit1.y < aabbPos.y - extent.y || suspectedHit1.z > aabbPos.z + extent.z || suspectedHit1.z < aabbPos.z - extent.z) && (k1 > 0);
			boolean k2Valid = !(suspectedHit2.y > aabbPos.y + extent.y || suspectedHit2.y < aabbPos.y - extent.y || suspectedHit2.z > aabbPos.z + extent.z || suspectedHit2.z < aabbPos.z - extent.z) && (k2 > 0);
			
			if(k1Valid && k2Valid) {
				//choose the lesser of the two
				xK = Math.min(k1, k2);
			}else if(k1Valid && !k2Valid) {
				//choose k1
				xK = k1;
			}else if(!k1Valid && k2Valid) {
				//choose k2
				xK = k2;
			}
		}
		//same for y
		if(ray.y != 0) {
			
			//these 2 ks are the intersections for the y planes
			float k1 = (-diff.y+extent.y)/(ray.y);
			float k2 = (-diff.y-extent.y)/(ray.y);
			
			//we need to verify that these ks lie in the aabb and not off of it
			Vector3f suspectedHit1 = new Vector3f(start).add(new Vector3f(ray).mul(k1));
			Vector3f suspectedHit2 = new Vector3f(start).add(new Vector3f(ray).mul(k2));
			//check the ys and zs
			boolean k1Valid = !(suspectedHit1.x > aabbPos.x + extent.x || suspectedHit1.x < aabbPos.x - extent.x || suspectedHit1.z > aabbPos.z + extent.z || suspectedHit1.z < aabbPos.z - extent.z) && (k1 > 0);
			boolean k2Valid = !(suspectedHit2.x > aabbPos.x + extent.x || suspectedHit2.x < aabbPos.x - extent.x || suspectedHit2.z > aabbPos.z + extent.z || suspectedHit2.z < aabbPos.z - extent.z) && (k2 > 0);
			if(k1Valid && k2Valid) {
				//choose the lesser of the two
				yK = Math.min(k1, k2);
			}else if(k1Valid && !k2Valid) {
				//choose k1
				yK = k1;
			}else if(!k1Valid && k2Valid) {
				//choose k2
				yK = k2;
			}
		}
		//same for z
		if(ray.z != 0) {
			//these 2 ks are the intersections for the z planes
			float k1 = (-diff.z+extent.z)/(ray.z);
			float k2 = (-diff.z-extent.z)/(ray.z);
			//we need to verify that these ks lie in the aabb and not off of it
			Vector3f suspectedHit1 = new Vector3f(start).add(new Vector3f(ray).mul(k1));
			Vector3f suspectedHit2 = new Vector3f(start).add(new Vector3f(ray).mul(k2));
			//check the ys and zs
			boolean k1Valid = !(suspectedHit1.x > aabbPos.x + extent.x || suspectedHit1.x < aabbPos.x - extent.x || suspectedHit1.y > aabbPos.y + extent.y || suspectedHit1.y < aabbPos.y - extent.y) && (k1 > 0);
			boolean k2Valid = !(suspectedHit2.x > aabbPos.x + extent.x || suspectedHit2.x < aabbPos.x - extent.x || suspectedHit2.y > aabbPos.y + extent.y || suspectedHit2.y < aabbPos.y - extent.y) && (k2 > 0);
			
			if(k1Valid && k2Valid) {
				//choose the lesser of the two
				zK = Math.min(k1, k2);
			}else if(k1Valid && !k2Valid) {
				//choose k1
				zK = k1;
			}else if(!k1Valid && k2Valid) {
				//choose k2
				zK = k2;
			}
		}
		float k = Float.MIN_VALUE;
		//determine the ks based on the xK, yK, and zK
		if(xK != Float.MIN_VALUE && yK != Float.MIN_VALUE && zK != Float.MIN_VALUE) {
			//all are valid, the final k is the min of the 3
			float min1 = Math.min(xK, yK);
			k = Math.min(min1, zK);
		}else if(xK == Float.MIN_VALUE && yK != Float.MIN_VALUE && zK != Float.MIN_VALUE) {
			//y and z are valid
			k = Math.min(yK, zK);
		}else if(xK == Float.MIN_VALUE && yK == Float.MIN_VALUE && zK != Float.MIN_VALUE) {
			//only z valid
			k = zK;
		}else if(xK != Float.MIN_VALUE && yK == Float.MIN_VALUE && zK != Float.MIN_VALUE) {
			//x and z are valid
			k = Math.min(xK, zK);
		}else if(xK != Float.MIN_VALUE && yK != Float.MIN_VALUE && zK == Float.MIN_VALUE) {
			//x and y are valid
			k = Math.min(xK, yK);
		}else if(xK == Float.MIN_VALUE && yK != Float.MIN_VALUE && zK == Float.MIN_VALUE) {
			//only y valid
			k = yK;
		}else if(xK != Float.MIN_VALUE && yK == Float.MIN_VALUE && zK == Float.MIN_VALUE) { 
			//only x valid
			k = xK;
		}
		return k;
	}
}
