package physics;

import org.joml.Vector3f;

public class ColliderCCD {
	public static void collide(Hull one, Hull two) {
		// split into all possible cases
		if (one.getClass() == CSphere.class) {
			// must use one of the sphere methods
			if (two.getClass() == CSphere.class) {
				// both spheres, can do sphere sphere
				sphere_sphere((CSphere) one, (CSphere) two);
			} else if(two.getClass() == COBB.class){
				// one sphere, one obb
				sphere_obb((CSphere) one, (COBB) two);
			}else if(two.getClass() == BarrierHull.class) {
				//one sphere, one barrier hull
				CSphere o = (CSphere) one;
				Barrier[] barriers = ((BarrierHull) two).getBarriers();
				for(Barrier b: barriers) collideBarrier(o, b);
			}
		} else if (one.getClass() == COBB.class) {
			// must use a cobb method
			if (two.getClass() == COBB.class) {
				// both obb, can do obb-obb
				obb_obb((COBB) one, (COBB) two);
			} else if(two.getClass() == CSphere.class){
				// one obb, one sphere
				sphere_obb((CSphere) two, (COBB) one);
			}else if(two.getClass() == BarrierHull.class) {
				//one sphere, one barrier hull
				COBB o = (COBB) one;
				Barrier[] barriers = ((BarrierHull) two).getBarriers();
				for(Barrier b: barriers) collideBarrier(o, b);
			}
		}
	}

	public static void collideBarrier(Hull one, Barrier two) {
		// split into hull types
		if (one.getClass() == CSphere.class) {
			// do sphere-barrier
			sphere_barrier((CSphere) one, two);
		} else if(one.getClass() == COBB.class){
			// do obb-barrier
			obb_barrier((COBB) one, two);
		}
	}

	private static void sphere_sphere(CSphere one, CSphere two) {
		
		float dt = visual.Launch.world.getDT();
		// use ccd to sweep both spheres through the timestep
		// position of a sphere is given by p0 + v0*dt*t
		// solving for toi has a quick solution
		// length of the difference vector has to be less than sum of radii for a
		// collision to occur
		// length(p0 + v0*dt*t - p1 - v1*dt*t) = r0 + r1
		Vector3f diff = new Vector3f(two.getPosition()).sub(one.getPosition());
		Vector3f velocityDiff = new Vector3f(two.getVelocity()).sub(one.getVelocity());
		// scale the velocity diff by dt to be a velocity step difference
		velocityDiff.mul(dt);
		// the previous expression can be rewritten
		// (pDiff + vDiff*dt*t).(pDiff + vDiff*dt*t) = (r0+r1)^2
		// expanding the dot product gives a quadratic in terms of t
		// a = (vDiff * dt)^2
		// b = 2 (pDiff.vDiff) * dt
		// c = pDiff.pDiff - (r0 + r1)^2
		float sumRadii = one.getRadius() + two.getRadius();
		float a = velocityDiff.dot(velocityDiff);
		float b = 2 * diff.dot(velocityDiff);
		float c = diff.dot(diff) - sumRadii * sumRadii;

		// solve the quadratic
		float det = b * b - 4 * a * c;
		if (det < 0) {
			// invalid solutions, no collision possible
			return;
		}
		// calc the 2 solutions
		float t1 = -b - (float) Math.sqrt(det);
		float t2 = -b + (float) Math.sqrt(det);
		if (a == 0)
			return;
		t1 /= 2 * a;
		t2 /= 2 * a;

		// find a valid t
		float t = Math.min(t1, t2);

		if (c < 0) {
			// calc the penetration depth
			float penetrationDepth1 = Math.abs(t1 * velocityDiff.dot(diff));
			float penetrationDepth2 = Math.abs(t2 * velocityDiff.dot(diff));

			// choose toi with the minimum
			t = (penetrationDepth1 < penetrationDepth2) ? t1 : t2;
		}

		if (c < 0 || (t >= -0 && t <= 1)) {
			// valid collision time
			sphere_sphere_respond(one, two, t, dt);
		}
	}

	private static void sphere_sphere_respond(CSphere one, CSphere two, float t, float dt) {
		Vector3f finalOne = new Vector3f(one.getPosition()).add(new Vector3f(one.getVelocity()).mul(dt * t));
		Vector3f finalTwo = new Vector3f(two.getPosition()).add(new Vector3f(two.getVelocity()).mul(dt * t));

		two.setPosition(finalTwo);
		one.setPosition(finalOne);

		Vector3f normal = new Vector3f(new Vector3f(two.getPosition()).sub(one.getPosition())).normalize();
		// calculate the relative velocity of each sphere
		Vector3f relVel1 = new Vector3f(one.getVelocity());
		Vector3f relVel2 = new Vector3f(two.getVelocity());
		// dot it with the normal to find the relevant portion of velocity that needs to
		// be canceled
		float relVel = normal.dot(new Vector3f(relVel2).sub(relVel1));

		float impulse = (1.5f) * (relVel / (1.0f / one.getMass() + 1.0f / two.getMass()));

		Vector3f linearImpulse = new Vector3f(normal).mul(impulse);
		one.addImpulse(new Vector3f(linearImpulse).mul(-1.0f));
		two.addImpulse(linearImpulse);

		// angular impulses don't really matter because a radius can always be drawn to
		// the torque application
		// aka no angular impulse
		// but we still call the apply impulse function to dampen the omegas
		one.addAngularImpulse(new Vector3f(0, 0, 0));
		two.addAngularImpulse(new Vector3f(0, 0, 0));
	}

	private static void sphere_obb(CSphere one, COBB two) {

	}

	private static void sphere_barrier(CSphere one, Barrier two) {
		if (two.getClass() == BoundedBarrier.class) {
			ColliderCCD.sphere_bbarrier(one, (BoundedBarrier) two);
			return;
		}
		// use ccd to sweep the spheres position through this timesteps velocity
		// then solve for the toi (if passing through barrier)
		// t is from clamped to (0->1)
		// position is given by p0 + v0*dt*t, where t is the time through the timestep
		// rotation is given by r0 + o0*dt*t, where t is the time through the timestep
		// velocity and omega are constant throughout the interval (unless responded to)
		float dt = visual.Launch.world.getDT();
		// we need to solve for when the position relative to the plane is less than the
		// radius of the sphere, because that means the sphere is intersecting the
		// radius
		// so n . (p0 + v0*dt*t) < r
		Vector3f diff = new Vector3f(one.getPosition()).sub(two.getPosition());
		Vector3f planeNormal = two.getNormal().normalize();
		Vector3f velocityStep = new Vector3f(one.getVelocity()).mul(dt);
		// solving for t
		// (r- n.p0)/(n.v0*dt)
		float t = one.getRadius() - planeNormal.dot(diff);
		// cache the negative of the distance value (positive = behind barrier, negative
		// equals in front)
		float dist = t;
		float divider = planeNormal.dot(velocityStep);
		//this divider may be 0 so we clamp it
		if(Math.abs(divider) < 0.0000001f) {
			divider = Math.signum(divider) * 1f;
		}
		t /= (divider);
		// ensure t is in the right range (or if the sphere is behind the barrier)
		if ((dist > 0.0001f) || (t >= 0 && t <= 1)) {

			// correct the position
			Vector3f positionStep = velocityStep.mul(t);
			// set the position of the sphere
			Vector3f newPosition = new Vector3f(one.getPosition());
			newPosition.add(positionStep);

			one.setPosition(newPosition);

			// calculate the relative velocity of the sphere
			// radius is different depending on whether the sphere is front of or behind the
			// barrier
			Vector3f radius = new Vector3f(planeNormal).mul(-Math.signum(dist) * one.getRadius());
			// calc tangential from crossing r and omega
			Vector3f tangentialVelocity = new Vector3f(radius).cross(one.getOmega());
			// relative is just the sum of the two velocities
			Vector3f relativeVelocity = new Vector3f(one.getVelocity()).add(tangentialVelocity);

			// now dot it with the normal to only get rid of the components that act into
			// the barrier
			float dot = planeNormal.dot(relativeVelocity);
			
			if(dot < 0) {
				relativeVelocity = new Vector3f(planeNormal).mul(dot);
				// calculate the impulse
				// impulse is the change in velocity * mass
				// the change in velocity is the relative velocity scale to some negative factor
				// (since want to cancel it)
				Vector3f impulse = relativeVelocity.mul(-1.000000001f * one.getMass());
				// apply a linear impulse
				one.addImpulse(impulse);
				// technically, angular impulse will always be 0 because of spherical symmetry
				// a radius can always be made to be in the same direction of the force, meaning
				// cross equates in 0 so no angular impulse
				one.addAngularImpulse(new Vector3f(0, 0, 0));
			}
		}
	}

	private static void sphere_bbarrier(CSphere one, BoundedBarrier two) {
		// similar to the infinite barrier but has bounds
		// the direction in which it is bounded is defined by the bounded barrier object

		Vector3f principalNormal = two.getNormal();
		Vector3f dir1 = two.getFirstOrientation();
		Vector3f dir2 = two.getSecondOrientation();

		// these 3 axes define the CCD required (the principal normal is the same)
		float dt = visual.Launch.world.getDT();

		Vector3f diff = new Vector3f(one.getPosition()).sub(two.getPosition());
		Vector3f velocityStep = new Vector3f(one.getVelocity()).mul(dt);
		float t = one.getRadius() - principalNormal.dot(diff);
		// cache the negative of the distance value (positive = behind barrier, negative
		// equals in front)
		float dist = t;
		float divider = principalNormal.dot(velocityStep);
		//this divider may be 0 so we clamp it
		if(Math.abs(divider) < 0.0000001f) {
			divider = Math.signum(divider) * 1f;
		}
		t /= (divider);
		
		float dir1Bounds = two.getXScale() + one.getRadius() + 0.01f;
		float dir2Bounds = two.getYScale() + one.getRadius() + 0.01f;
		boolean evenPossible = eval_dir(dir1, dir1Bounds, diff, one.getVelocity(), dt) && eval_dir(dir2, dir2Bounds, diff, one.getVelocity(), dt);

		// ensure t is in the right range (or if the sphere is behind the barrier)
		if (evenPossible && ((dist > 0f && dist < 0.2f) || (t >= 0.01f && t <= 1))) {
			// correct the position
			Vector3f positionStep = velocityStep.mul(t);
			// set the position of the sphere
			Vector3f newPosition = new Vector3f(one.getPosition());
			newPosition.add(positionStep);

			one.setPosition(newPosition);

			// calculate the relative velocity of the sphere
			// radius is different depending on whether the sphere is front of or behind the
			// barrier
			Vector3f radius = new Vector3f(principalNormal).mul(-Math.signum(dist) * one.getRadius());
			// calc tangential from crossing r and omega
			Vector3f tangentialVelocity = new Vector3f(radius).cross(one.getOmega());
			// relative is just the sum of the two velocities
			Vector3f relativeVelocity = new Vector3f(one.getVelocity()).add(tangentialVelocity);
			
			// now dot it with the normal to only get rid of the components that act into
			// the barrier
			float dot = principalNormal.dot(relativeVelocity);
			if(dot < 0) {
				relativeVelocity = new Vector3f(principalNormal).mul(dot);
	
				// calculate the impulse
				// impulse is the change in velocity * mass
				// the change in velocity is the relative velocity scale to some negative factor
				// (since want to cancel it)
				Vector3f impulse = relativeVelocity.mul(-1.00000001f * one.getMass());
				// apply a linear impulse
				one.addImpulse(impulse);
				// technically, angular impulse will always be 0 because of spherical symmetry
				// a radius can always be made to be in the same direction of the force, meaning
				// cross equates in 0 so no angular impulse
				one.addAngularImpulse(new Vector3f(0, 0, 0));
			}
		}
	}
	
	private static boolean eval_dir(Vector3f dir, float scale, Vector3f diff, Vector3f v, float dt) {
		/*
		float dot = diff.dot(dir);
		if(dot < scale && dot > -scale) return true;
		retu fase;
		*/
		//position of point relative to the direction
		//(p+v*dt*t).dir
		//the equality we need to check for is 
		// -scale < p < scale
		
		float current = diff.dot(dir);
		float step = new Vector3f(v).mul(dt).dot(dir);
		
		float t1 = (-scale - current) / step;
		float t2 = ( scale - current) / step;
		
		//clamp the ts
		t1 = Math.min(t1, Float.MAX_VALUE);
		t1 = Math.max(t1, -Float.MAX_VALUE);
		
		t2 = Math.min(t2, Float.MAX_VALUE);
		t2 = Math.max(t2, -Float.MAX_VALUE);
		
		//check for validity
		if(t1 < 0 && t2 < 0) {
			//already past, not possible
			return false;
		}else if(t1 > 1 && t2 > 1) {
			//too far, not possible
			return false;
		}else {
			//in the middle, either touching in the time step or in time step
			return true;
		}
	}

	private static void obb_obb(COBB one, COBB two) {
		
	}

	private static void obb_barrier(COBB one, Barrier two) {

	}
}
