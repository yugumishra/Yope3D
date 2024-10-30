package scripts;

import org.joml.Vector3f;

import physics.Sphere;
import visual.PointLight;
import visual.Util.STATES;
import visual.Vector;

public class PushForward extends Script {
	Vector3f accumulation;
	float angle1 = 0.0f;
	float angle2 = 0.0f;
	float targetAngle1;
	float targetAngle2;

	@Override
	public void init() {
		accumulation = new Vector3f(0,0,0);
		super.init();
		
		angle1 = 0.0f;
		angle2 = 0.0f;
		
		targetAngle1 = 3.14159265359f;
		targetAngle2 = 3.14159265359f;
		
		loop.getCamera().setPosition(new Vector3f(0,0,5));
		
		world.addLight(new PointLight(new Vector3f(), new Vector3f(1,1,1), new Vector3f(1,0,0)));
		
		world.setDT(1.0f/60.0f);
		
		Sphere dummy = Sphere.genSphere(3, 1.0f);
		dummy.setDraw(false);
		world.addMesh(dummy);
		world.addMesh(dummy);
	}
	
	@Override
	public void update() {
		if(true) {
			world.removeMesh(1);
			//world.removeMesh(0);
			if(loop.frames() % 60 == 0) {
				angle1 = targetAngle1;
				angle2 = targetAngle2;
				targetAngle1 = 2.0f * (float) Math.random() * 3.14159265359f;
				targetAngle2 = 2.0f * (float) Math.random() * 3.14159265359f;
			}
			
			int interpolation = loop.frames() % 60;
			float timeValue = interpolation / 60.0f;
			
			float diffAngle1 = targetAngle1 - angle1;
			float diffAngle2 = targetAngle2 - angle2;
			
			float usedAngle1 = angle1 + diffAngle1 * timeValue;
			float usedAngle2 = angle2 + diffAngle2 * timeValue;
			
			float y = (float) Math.sin(usedAngle2);
			float nar = (float) Math.cos(usedAngle2);
			float x = nar * (float) Math.cos(usedAngle1);
			float z = nar * (float) Math.sin(usedAngle1);
			
			Vector3f forward = new Vector3f(x,y,z);
			forward.normalize();
			
			Sphere sphere = Sphere.genSphere(3, 0.5f);
			Vector vector = Vector.genVector(forward);
			
			sphere.setColor(0.0f, 1.0f, 1.0f);
			vector.setColor(0.0f, 0.5f, 0.5f);
			vector.setState(STATES.SOLID_COLOR);
			sphere.setState(STATES.LIGHT);
			
			sphere.getHull().setPosition(accumulation);

			vector.getHull().setPosition(new Vector3f(accumulation).add(forward));
			accumulation.add(new Vector3f(forward).mul(3 * world.getDT()));
			
			world.removeLight(0);
			world.addLight(new PointLight(new Vector3f(sphere.getHull().getPosition()), new Vector3f(1,1,1), new Vector3f(1,0,0)));
			
			vector.loadMesh();
			sphere.loadMesh();
			world.addMesh(sphere);
			world.addMesh(vector);
		}
	}
}
