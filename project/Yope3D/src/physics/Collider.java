package physics;

import org.joml.Matrix3f;
import org.joml.Vector3f;

import scripts.Tester;

public class Collider {

	public static boolean detectSphereSphere(Sphere a, Sphere b) {
		float distanceSquared = new Vector3f(b.getPosition()).sub(a.getPosition()).lengthSquared();
		float radiusSum = a.getRadius() + b.getRadius();
		return distanceSquared <= radiusSum * radiusSum;
	}

	public static boolean detectSphereAABB(Sphere a, AABB b) {
		Vector3f closestPosition = new Vector3f();
		Vector3f aabbMin = new Vector3f(b.getPosition()).sub(b.getScales());
		Vector3f aabbMax = new Vector3f(b.getPosition()).add(b.getScales());
		closestPosition.x = Math.max(aabbMin.x, Math.min(aabbMax.x, a.getPosition().x));
		closestPosition.y = Math.max(aabbMin.y, Math.min(aabbMax.y, a.getPosition().y));
		closestPosition.z = Math.max(aabbMin.z, Math.min(aabbMax.z, a.getPosition().z));

		Vector3f diff = new Vector3f(a.getPosition()).sub(closestPosition);

		return diff.lengthSquared() <= a.getRadius() * a.getRadius();
	}

	public static boolean detectSphereOBB(Sphere a, OBB b) {
		Vector3f posA = a.getPosition();
		Vector3f posB = b.getPosition();
		Vector3f[] obbAxes = b.getOBBAxes();
		Vector3f obbScale = b.getScales();
		float radius = a.getRadius();

		Vector3f diff = new Vector3f(posA).sub(posB);

		// sat test: test obb axes
		for (int i = 0; i < 3; i++) {
			Vector3f axis = obbAxes[i];

			float dist = Math.abs(new Vector3f(diff).dot(axis));
			float overlap = obbScale.dot(new Vector3f(axis).absolute()) + radius - dist;

			if (overlap <= 0)
				return false;
		}

		return true;
	}

	public static boolean detectAABBOBB(AABB a, OBB b) {
		Vector3f posA = a.getPosition();
		Vector3f posB = b.getPosition();
		Vector3f aScales = a.getScales();
		Vector3f[] obbAxes = b.getOBBAxes();
		Vector3f obbScales = b.getScales();

		Vector3f diff = new Vector3f(posA).sub(posB);

		// first check the aabb axes
		for (int i = 0; i < 3; i++) {
			Vector3f axis = new Vector3f(0, 0, 0);
			axis.setComponent(i, 1);

			float dist = Math.abs(diff.dot(axis));
			float overlap = aScales.get(i) + obbScales.dot(axis.absolute()) - dist;

			if (overlap < 0)
				return false;
		}

		// then check the obb axes
		for (int i = 0; i < 3; i++) {
			Vector3f axis = obbAxes[i];

			float dist = Math.abs(diff.dot(axis));
			float overlap = aScales.dot(axis.absolute()) + obbScales.get(i) - dist;

			if (overlap < 0)
				return false;
		}

		// then check the cross product of the axes
		for (int i = 0; i < 3; i++) {
			Vector3f aAxis = new Vector3f(0, 0, 0);
			aAxis.setComponent(i, 1);

			for (int j = 0; j < 3; j++) {
				Vector3f axis = aAxis.cross(obbAxes[j], new Vector3f());
				if (axis.lengthSquared() < 1e-6)
					continue; // skip near 0

				axis.normalize();
				float dist = Math.abs(diff.dot(axis));
				float overlap = aScales.dot(axis.absolute()) + obbScales.dot(axis.absolute()) - dist;

				if (overlap < 0)
					return false;
			}
		}

		return true;
	}

	public static boolean detectOBBOBB(OBB a, OBB b) {
		Vector3f posA = a.getPosition();
		Vector3f posB = b.getPosition();
		Vector3f[] aAxes = a.getOBBAxes();
		Vector3f[] bAxes = b.getOBBAxes();
		Vector3f aScale = a.getScales();
		Vector3f bScale = b.getScales();

		Vector3f diff = new Vector3f(posB).sub(posA);

		for (int i = 0; i < 3; i++) {
			Vector3f axis = aAxes[i];
			float dist = new Vector3f(diff).dot(axis);

			if (dist > axisTestOBBOBB(aAxes[i], aAxes, bAxes, aScale, bScale))
				return false;

			axis = bAxes[i];
			dist = new Vector3f(diff).dot(axis);

			if (dist > axisTestOBBOBB(bAxes[i], aAxes, bAxes, aScale, bScale))
				return false;
			for (int j = 0; j < 3; j++) {
				Vector3f crossAxis = aAxes[i].cross(bAxes[j], new Vector3f());
				if (crossAxis.lengthSquared() < 1e-4)
					continue;

				axis.normalize();
				dist = new Vector3f(diff).dot(axis);
				if (dist > axisTestOBBOBB(crossAxis, aAxes, bAxes, aScale, bScale))
					return false;
			}
		}

		return true;
	}

	private static float axisTestOBBOBB(Vector3f axis, Vector3f[] aAxes, Vector3f[] bAxes, Vector3f aScale,
			Vector3f bScale) {
		float projA = projectOBB(aScale, aAxes, axis);
		float projB = projectOBB(bScale, aAxes, axis);

		return (projA + projB);
	}

	private static float projectOBB(Vector3f scale, Vector3f[] axes, Vector3f axis) {
		return Math.abs(scale.x * axes[0].dot(axis)) + Math.abs(scale.y * axes[1].dot(axis))
				+ Math.abs(scale.z * axes[2].dot(axis));
	}

	public static boolean detectAABBAABB(AABB a, AABB b) {
		// find the extents of each aabb
		Vector3f diff = new Vector3f(a.getPosition()).sub(b.getPosition());
		Vector3f scaleSum = new Vector3f(a.getScales()).add(b.getScales());

		return !(Math.abs(diff.x) > scaleSum.x || Math.abs(diff.y) > scaleSum.y || Math.abs(diff.z) > scaleSum.z);
	}

	public static void collideSphereSphere(Sphere a, Sphere b) {
		if (a.fixed() && b.fixed())
			return;

		Vector3f diff = new Vector3f(b.getPosition()).sub(a.getPosition());
		float length = diff.length();
		if (length < 0.0001f)
			length = 0.0001f; // clamp length to avoid div by 0

		// find collision normal (direction along which the collision is occuring)
		Vector3f collisionNormal = diff.div(length);

		// position correction
		// find the penetration (how much the spheres are inside each other)
		float penetration = a.getRadius() + b.getRadius() - length;

		// only fix if they are within each other
		if (penetration < 0)
			return;

		genericCollisionResponse(a, b, collisionNormal, penetration, new Vector3f[] { new Vector3f() });

	}

	public static void collideSphereAABB(Sphere a, AABB b) {
		// find closest point to sphere on the surface of aabb
		Vector3f closestPosition = new Vector3f();
		Vector3f aabbMin = new Vector3f(b.getPosition()).sub(b.getScales());
		Vector3f aabbMax = new Vector3f(b.getPosition()).add(b.getScales());
		closestPosition.x = Math.max(aabbMin.x, Math.min(aabbMax.x, a.getPosition().x));
		closestPosition.y = Math.max(aabbMin.y, Math.min(aabbMax.y, a.getPosition().y));
		closestPosition.z = Math.max(aabbMin.z, Math.min(aabbMax.z, a.getPosition().z));

		// use the difference between the closest point and the sphere to find the
		// collision direction
		Vector3f collisionNormal = new Vector3f(a.getPosition()).sub(closestPosition);
		float dist = collisionNormal.length();

		if (dist < 0.0001f)
			dist = 0.0001f; // clamp length to avoid div by 0

		collisionNormal.div(dist);

		// position correction

		// calculate the penetration of the objects
		float penetration = a.getRadius() - dist;

		// correct them if they are penetrating
		if (penetration < 0)
			return;

		genericCollisionResponse(a, b, collisionNormal.negate(), penetration, new Vector3f[] { closestPosition });

	}

	public static void collideAABBAABB(AABB a, AABB b) {
		if (a.fixed() && b.fixed())
			return;
		// find the axis of min penetration
		Vector3f posA = a.getPosition();
		Vector3f posB = b.getPosition();
		Vector3f scalesA = a.getScales();
		Vector3f scalesB = b.getScales();
		Vector3f aMin = new Vector3f(posA).sub(scalesA);
		Vector3f aMax = new Vector3f(posA).add(scalesA);
		Vector3f bMin = new Vector3f(posB).sub(scalesB);
		Vector3f bMax = new Vector3f(posB).add(scalesB);

		Vector3f overlap = new Vector3f();
		overlap.x = Math.min(aMax.x, bMax.x) - Math.max(aMin.x, bMin.x);
		overlap.y = Math.min(aMax.y, bMax.y) - Math.max(aMin.y, bMin.y);
		overlap.z = Math.min(aMax.z, bMax.z) - Math.max(aMin.z, bMin.z);

		float minOverlap = Math.min(Math.min(overlap.x, overlap.y), overlap.z);
		Vector3f collisionNormal = new Vector3f();
		int axis = 0;
		if (minOverlap == overlap.x) {
			if (a.getPosition().x < b.getPosition().x) {
				collisionNormal.x = 1;
			} else {
				collisionNormal.x = -1;
			}

		} else if (minOverlap == overlap.y) {
			if (a.getPosition().y < a.getPosition().y) {
				collisionNormal.y = 1;
			} else {
				collisionNormal.y = -1;
			}
			axis = 1;
		} else {
			if (a.getPosition().z < b.getPosition().z) {
				collisionNormal.z = 1;
			} else {
				collisionNormal.z = -1;
			}
			axis = 2;
		}

		// correct
		if (minOverlap < 0)
			return;

		Vector3f[] incFace = new Vector3f[4];
		for (int i = 0; i < 4; i++) {
			Vector3f p = new Vector3f();
			int dim1 = (((i & 2) != 0) ? (1) : (-1));
			int dim2 = (((i & 1) != 0) ? (1) : (-1));

			switch (axis) {
			case 0: // X-axis normal = Y-Z face
				p.y = b.getScales().y * dim1;
				p.z = b.getScales().z * dim2;
				break;
			case 1: // Y-axis normal = X-Z face
				p.x = b.getScales().x * dim1;
				p.z = b.getScales().z * dim2;
				break;
			case 2: // Z-axis normal = X-Y face
				p.x = b.getScales().x * dim1;
				p.y = b.getScales().y * dim2;
				break;
			}
			incFace[i] = p.add(b.getPosition());
		}

		// aabb axes clipping (really easy)
		for (int i = 0; i < incFace.length; i++) {
			for (int j = 0; j < 3; j++) {
				if (j == axis)
					continue;
				float minBound = posA.get(j) - scalesA.get(j);
				float maxBound = posA.get(j) + scalesA.get(j);

				// Clip against AABB bounds
				incFace[i].setComponent(j, Math.max(incFace[i].get(j), minBound));
				incFace[i].setComponent(j, Math.min(incFace[i].get(j), maxBound));
			}

			// Clip along the collision axis
			float distance = Math.abs(incFace[i].get(axis) - posA.get(axis));
			if (distance > scalesA.get(axis) + 1e-6) {
				incFace[i] = null; // Remove points outside the face
			}
		}

		genericCollisionResponse(a, b, collisionNormal, minOverlap, incFace);
	}

	public static void collideSphereOBB(Sphere a, OBB b) {
		Vector3f posA = a.getPosition();
		Vector3f posB = b.getPosition();
		Vector3f[] obbAxes = b.getOBBAxes(); // Local axes of the OBB
		Vector3f obbScale = b.getScales(); // Half-sizes of the OBB
		float radius = a.getRadius();

		Vector3f diff = new Vector3f(posB).sub(posA);

		float leastOverlap = Float.MAX_VALUE;
		Vector3f collisionNormal = new Vector3f();
		boolean collisionDetected = false;

		// SAT Test: Sphere axes and OBB axes
		for (int i = 0; i < 3; i++) {
			Vector3f axis = obbAxes[i];

			float dist = Math.abs(new Vector3f(diff).dot(axis));
			float overlap = obbScale.dot(new Vector3f(axis).absolute()) + radius - dist;

			if (overlap > 0) {
				collisionDetected = true;
				if (overlap < leastOverlap) {
					leastOverlap = overlap;
					collisionNormal.set(axis).mul(Math.signum(new Vector3f(diff).dot(axis)));
				}
			} else {
				return;
			}
		}

		if (!collisionDetected)
			return;

		// calculate contact point between speher and obb (only one bc sphere)
		Vector3f contactPoint = new Vector3f(b.getPosition());
		Vector3f localPoint = new Vector3f(a.getPosition()).sub(b.getPosition());

		for (int i = 0; i < 3; i++) {
			float distance = localPoint.dot(obbAxes[i]);
			distance = Math.max(-obbScale.get(i), Math.min(obbScale.get(i), distance)); // Clamp to OBB face
			contactPoint.add(new Vector3f(obbAxes[i]).mul(distance));
		}

		// resolve collision
		genericCollisionResponse(a, b, collisionNormal, leastOverlap, new Vector3f[] { contactPoint });
	}

	public static void collideAABBOBB(AABB a, OBB b) {
		Vector3f posA = a.getPosition();
		Vector3f posB = b.getPosition();
		Vector3f aScales = a.getScales();
		Vector3f[] obbAxes = b.getOBBAxes();
		Vector3f obbScales = b.getScales();

		Vector3f diff = new Vector3f(posB).sub(posA);
		float leastOverlap = Float.MAX_VALUE;
		Vector3f collisionNormal = new Vector3f();
		boolean collisionDetected = false;

		int axisIndex = 0;

		// test aabb axes first
		for (int i = 0; i < 3; i++) {
			Vector3f axis = new Vector3f(0, 0, 0);
			axis.setComponent(i, 1);

			float dist = Math.abs(diff.dot(axis));
			float overlap = aScales.get(i) + obbScales.dot(axis.absolute()) - dist;

			if (overlap > 0) {
				collisionDetected = true;
				if (overlap < leastOverlap) {
					leastOverlap = overlap;
					collisionNormal.set(axis).mul(Math.signum(diff.dot(axis)));
					axisIndex = i;
				}
			} else {
				return;
			}
		}

		// then obb axes
		for (int i = 0; i < 3; i++) {
			Vector3f axis = obbAxes[i];

			float dist = Math.abs(diff.dot(axis));
			float overlap = aScales.dot(axis.absolute()) + obbScales.get(i) - dist;

			if (overlap > 0) {
				collisionDetected = true;
				if (overlap < leastOverlap) {
					leastOverlap = overlap;
					collisionNormal.set(axis).mul(Math.signum(diff.dot(axis)));
					axisIndex = 3 + i;
				}
			} else {
				return;
			}
		}

		// then their crossproducts
		for (int i = 0; i < 3; i++) {
			Vector3f aAxis = new Vector3f(0, 0, 0);
			aAxis.setComponent(i, 1);

			for (int j = 0; j < 3; j++) {
				Vector3f axis = aAxis.cross(obbAxes[j], new Vector3f());
				if (axis.lengthSquared() < 1e-6)
					continue; // skip near 0

				axis.normalize();
				float dist = Math.abs(diff.dot(axis));
				float overlap = aScales.dot(axis.absolute()) + obbScales.dot(axis.absolute()) - dist;

				if (overlap > 0) {
					collisionDetected = true;
					if (overlap < leastOverlap) {
						leastOverlap = overlap;
						collisionNormal.set(axis).mul(Math.signum(diff.dot(axis)));
						axisIndex = 6 + 3 * i + j;
					}
				} else {
					return;
				}
			}
		}

		if (!collisionDetected) {
			return;
		}

		// find reference and incident faces
		Vector3f[] refFace = new Vector3f[4], incFace = new Vector3f[4];
		
		if (axisIndex < 3) {
			Vector3f[] points = b.worldSpace();
			for(int i = 0; i< 4; i++) {
				float minValue = Float.MAX_VALUE;
				int ind = 0;
				for(int j = 0; j< 8; j++) {
					if(points[j] == null) continue;
					float dot = (points[j]).dot(collisionNormal);
					if(dot < minValue - 1e-2) {
						minValue = dot;
						ind = j;
					}
				}
				incFace[i] = new Vector3f(points[ind]);
				points[ind] = null;
			}
		}else if(axisIndex < 6) {
			
		}else {
			//edge-edge
		}

		// sutherland-hodgeman clipping to create the contact manifold
		if (axisIndex < 3) {
			// aabb axes clipping (really easy)
			for (int i = 0; i < incFace.length; i++) {
				for (int j = 0; j < 3; j++) {
					if (j == axisIndex)
						continue;
					float minBound = posA.get(j) - aScales.get(j);
					float maxBound = posA.get(j) + aScales.get(j);

					// Clip against AABB bounds
					incFace[i].setComponent(j, Math.max(incFace[i].get(j), minBound));
					incFace[i].setComponent(j, Math.min(incFace[i].get(j), maxBound));
				}

				// Clip along the collision axis
				float distance = Math.abs(incFace[i].get(axisIndex) - posA.get(axisIndex));
				if (distance > aScales.get(axisIndex) + 1e-6) {
					incFace[i] = null; // Remove points outside the face
				}
			}
		}

		Tester t = (Tester) visual.Launch.world.getScript(0);
		for (int i = 0; i < 4; i++) {
			Vector3f p = (incFace[i] == null) ? (new Vector3f(100000, 10000, 100000)) : (new Vector3f(incFace[i]));
			t.incFace[i] = p;
		}

		collisionNormal.normalize();

		genericCollisionResponse(a, b, collisionNormal, leastOverlap, incFace);
	}

	public static void collideOBBOBB(OBB a, OBB b) {

	}

	private static void genericCollisionResponse(Hull a, Hull b, Vector3f collisionNormal, float penetration,
			Vector3f[] contactPoints) {
		pgsCollisionResponse(a, b, collisionNormal, penetration, contactPoints);
	}

	private static void pgsCollisionResponse(Hull a, Hull b, Vector3f collisionNormal, float penetration,
			Vector3f[] contactPoints) {
		int numContacts = contactPoints.length;
		if (numContacts == 0)
			return;

		int numIterations = (numContacts == 1) ? (1) : (8); // Adjust based on stability needs

		float[] lambda = new float[numContacts]; // Impulse magnitudes
		float[] W = new float[numContacts]; // Effective mass per constraint
		float[] neta = new float[numContacts];

		Matrix3f IinvA = a.getInverseInertiaTensorWorld();
		Matrix3f IinvB = b.getInverseInertiaTensorWorld();
		
		// Compute the Jacobian and effective mass matrix once before iteration
		for (int i = 0; i < numContacts; i++) {
			Vector3f contact = contactPoints[i];
			if (contact == null)
				continue;

			Vector3f rA = new Vector3f(contact).sub(a.getPosition());
			Vector3f rB = new Vector3f(contact).sub(b.getPosition());

			// Linear terms
			float invMassA = a.getInverseMass();
			float invMassB = b.getInverseMass();

			// Angular terms
			Vector3f angularA = new Vector3f();
			Vector3f angularB = new Vector3f();

			if (!a.fixed()) {
				Vector3f raCrossN = new Vector3f(rA).cross(collisionNormal);
				angularA = new Vector3f(IinvA.transform(raCrossN)).cross(rA);
			}

			if (!b.fixed()) {
				Vector3f rbCrossN = new Vector3f(rB).cross(collisionNormal);
				angularB = new Vector3f(IinvB.transform(rbCrossN)).cross(rB);
			}

			// Compute effective mass W (J M^-1 J^T)
			float effectiveMass = invMassA + invMassB + angularA.dot(collisionNormal) + angularB.dot(collisionNormal);
			if (effectiveMass < 1e-6f)
				continue; // Avoid division by zero

			W[i] = 1.0f / effectiveMass;

			// Bias to handle penetration (Baumgarte stabilization)
			float bias = 0.2f / visual.Launch.world.getDT() * Math.max(0.0f, penetration - 3e-2f);

			neta[i] = (bias);
		}

		// **Iterate through constraints multiple times for better convergence**
		for (int iter = 0; iter < numIterations; iter++) {
			for (int i = 0; i < numContacts; i++) {
				Vector3f contact = contactPoints[i];
				if (contact == null)
					continue;

				Vector3f rA = new Vector3f(contact).sub(a.getPosition());
				Vector3f rB = new Vector3f(contact).sub(b.getPosition());

				// Compute relative velocity
				Vector3f relVel = new Vector3f(b.getVelocity()).sub(a.getVelocity());
				Vector3f tangential = new Vector3f();
				if (!a.fixed())
					tangential.add(new Vector3f(a.getOmega()).cross(rA));
				if (!b.fixed())
					tangential.sub(new Vector3f(b.getOmega()).cross(rB));

				relVel.add(tangential);

				float velocityOnNormal = 0.5f * relVel.dot(collisionNormal);

				// Compute desired lambda change (impulse magnitude)
				float deltaLambda = -W[i] * (velocityOnNormal - neta[i]);

				// Projection: Clamp lambda to be non-negative (non-penetration constraint)
				float newLambda = Math.max(0, lambda[i] + deltaLambda);
				float impulseChange = newLambda - lambda[i];
				lambda[i] = newLambda;

				// Apply impulses immediately for each contact (this propagates changes)
				Vector3f impulse = new Vector3f(collisionNormal).mul(impulseChange);
				a.addImpulse(impulse.negate());
				b.addImpulse(impulse.negate());

				if (!a.fixed()) {
					Vector3f angularImpulseA = new Vector3f(rA).cross(impulse);
					a.addAngularImpulse(angularImpulseA);
				}

				if (!b.fixed()) {
					Vector3f angularImpulseB = new Vector3f(rB).cross(impulse);
					b.addAngularImpulse(angularImpulseB.negate());
				}

				a.applyImpulses();
				b.applyImpulses();
			}

		}

	}

}
