package physics;

import org.joml.Vector3f;

//rectangular barrier with bounds
public class BoundedBarrier extends Barrier {
	private float xScale;
	private float yScale;
	private Vector3f orientation;
	
	public BoundedBarrier(float xScale, float yScale, Vector3f normal, Vector3f orientation, Vector3f pos) {
		super(normal, pos);
		this.orientation = new Vector3f(orientation);
		this.xScale = xScale;
		this.yScale = yScale;
	}
	
	public float getXScale() {
		return xScale;
	}
	
	public float getYScale() {
		return yScale;
	}
	
	public Vector3f getFirstOrientation() {
		return orientation;
	}
	
	public Vector3f getSecondOrientation() {
		return new Vector3f(getNormal()).cross(orientation);
	}
	
	public void setXScale(float n) {
		xScale = n;
	}
	
	public void setYScale(float n) {
		yScale = n;
	}
	
	public void setOrientation1(Vector3f n) {
		orientation = new Vector3f(n);
	}
	
	public static BarrierHull genRectangularBarriers(Vector3f extent, Vector3f pos) {
		BoundedBarrier[] barriers = {
				new BoundedBarrier(extent.y, extent.z, new Vector3f( 1,0,0), new Vector3f(0,1,0), new Vector3f(pos.x + extent.x, pos.y, pos.z)),
				//new BoundedBarrier(extent.y, extent.z, new Vector3f(-1,0,0), new Vector3f(0,1,0), new Vector3f(pos.x + extent.x, pos.y, pos.z)),
				
				new BoundedBarrier(extent.y, extent.z, new Vector3f(-1,0,0), new Vector3f(0,1,0), new Vector3f(pos.x - extent.x, pos.y, pos.z)),
				//new BoundedBarrier(extent.y, extent.z, new Vector3f(-1,0,0), new Vector3f(0,1,0), new Vector3f(pos.x - extent.x, pos.y, pos.z)),
				
				
				new BoundedBarrier(extent.x, extent.y, new Vector3f(0,0, 1), new Vector3f(1,0,0), new Vector3f(pos.x, pos.y, pos.z + extent.z)),
				//new BoundedBarrier(extent.x, extent.y, new Vector3f(0,0,-1), new Vector3f(1,0,0), new Vector3f(pos.x, pos.y, pos.z + extent.z)),
				
				new BoundedBarrier(extent.x, extent.y, new Vector3f(0,0,-1), new Vector3f(1,0,0), new Vector3f(pos.x, pos.y, pos.z - extent.z)),
				//new BoundedBarrier(extent.x, extent.y, new Vector3f(0,0,-1), new Vector3f(1,0,0), new Vector3f(pos.x, pos.y, pos.z - extent.z)),
				
				
				new BoundedBarrier(extent.x, extent.z, new Vector3f(0, 1,0), new Vector3f(1,0,0), new Vector3f(pos.x, pos.y + extent.y, pos.z)),
				//new BoundedBarrier(extent.x, extent.z, new Vector3f(0,-1,0), new Vector3f(1,0,0), new Vector3f(pos.x, pos.y + extent.y, pos.z)),
				
				new BoundedBarrier(extent.x, extent.z, new Vector3f(0,-1,0), new Vector3f(1,0,0), new Vector3f(pos.x, pos.y - extent.y, pos.z)),
				//new BoundedBarrier(extent.x, extent.z, new Vector3f(0,-1,0), new Vector3f(1,0,0), new Vector3f(pos.x, pos.y - extent.y, pos.z)),
				
		};
		return new BarrierHull(barriers, pos, new Vector3f(extent));
	}
	
	public static BarrierHull genBHSingle(BoundedBarrier b) {
		Vector3f first = b.getFirstOrientation();
		Vector3f second = b.getSecondOrientation();
		first.mul(b.getXScale());
		second.mul(b.getYScale());
		Vector3f extent = new Vector3f(b.getPosition()).add(first).add(second);
		System.out.println(extent);
		//find the extremities
		Barrier[] barriers = {b};
		return new BarrierHull(barriers, b.getPosition(), extent);
	}
	
	public static BarrierHull genBHSingle(BoundedBarrier b, Vector3f extent) {
		Barrier[] barriers = {b};
		return new BarrierHull(barriers, b.getPosition(), extent);
	}
}
