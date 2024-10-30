package physics;

import org.joml.Matrix3f;
import org.joml.Vector3f;

//just a group of barriers
public class BarrierHull extends Hull {
	
	//list of barriers that defines this hull
	private Barrier[] barriers;
	
	//extent of this barrier hull
	private Vector3f extent;

	public BarrierHull(Barrier[] barriers, Vector3f pos, Vector3f extent) {
		super(pos, new Vector3f(), 0.0f, new Vector3f(), new Vector3f());
		this.barriers = barriers;
		this.extent = extent;
	}
	
	public Barrier[] getBarriers() {
		return barriers;
	}
	
	public Vector3f getExtent() {
		return extent;
	}
	
	public void setExtent(Vector3f n) {
		this.extent = n;
	}
	
	public void setBarriers(Barrier[] n) {
		barriers = n;
	}
	
	@Override
	public Matrix3f genInertiaTensor() {
		return null;
	}
}
