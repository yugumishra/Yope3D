package physics;

public class Collider {
	public static void CCD(Hull one, Hull two) {
		ColliderCCD.collide(one, two);
	}
	
	public static void CCDBarrier(Hull one, Barrier two) {
		ColliderCCD.collideBarrier(one, two);
	}
}
