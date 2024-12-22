package visual;

import org.joml.Vector3f;

public class FlashLight extends SpotLight {

	public FlashLight(float innerConeAngle, float outerConeAngle,
			Vector3f lightColor, Vector3f lightCharacteristics) {
		super(null, new Vector3f(0,0,0), innerConeAngle, outerConeAngle, lightColor, lightCharacteristics);
	}

}
