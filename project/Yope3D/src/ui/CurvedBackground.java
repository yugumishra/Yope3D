package ui;

import java.util.ArrayList;
import java.util.List;

import visual.*;

public class CurvedBackground extends Background {
	
	protected float xs;

	public CurvedBackground(float r, float g, float b, int level) {
		super(r, g, b, level);
	}

	@Override
	public void redefineMesh() {
		float aspectRatio = (float) Launch.window.getWidth() / (float) Launch.window.getHeight();
		int subdiv = 64;
		List<Float> vertices = new ArrayList<Float>();
		List<Integer> indices = new ArrayList<Integer>();

		vertices.add(min.x);
		vertices.add(min.y);
		if(FLOATS_PER_VERTEX == 8) {
			// add filler floats
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
		}
		vertices.add(0.0f);
		vertices.add(0.0f);

		vertices.add(max.x);
		vertices.add(min.y);
		if(FLOATS_PER_VERTEX == 8) {
			// add filler floats
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
		}
		vertices.add(0.0f);
		vertices.add(0.0f);

		vertices.add(max.x);
		vertices.add(max.y);
		if(FLOATS_PER_VERTEX == 8) {
			// add filler floats
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
		}
		vertices.add(0.0f);
		vertices.add(0.0f);

		vertices.add(min.x);
		vertices.add(max.y);
		if(FLOATS_PER_VERTEX == 8) {
			// add filler floats
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
		}
		vertices.add(0.0f);
		vertices.add(0.0f);

		indices.add(0);
		indices.add(1);
		indices.add(2);
		indices.add(2);
		indices.add(0);
		indices.add(3);

		// half circle init
		float radius = min.y - max.y;
		radius *= 0.5f;

		// center 1
		vertices.add(min.x);
		vertices.add(min.y - radius);
		if(FLOATS_PER_VERTEX == 8) {
			// add filler floats
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
		}
		vertices.add(0.0f);
		vertices.add(0.0f);

		// center 2
		vertices.add(max.x);
		vertices.add(min.y - radius);
		if(FLOATS_PER_VERTEX == 8) {
			// add filler floats
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
			vertices.add(0.0f);
		}
		vertices.add(0.0f);
		vertices.add(0.0f);

		float theta = 0.0f;
		float p = 3.1415926539f;
		float thetaStep = p;
		thetaStep *= 2;
		thetaStep /= subdiv;

		for (int i = 0; i < subdiv; i++) {
			float c = xs * radius * (float) Math.cos(theta);
			c /= aspectRatio;
			float s = radius * (float) Math.sin(theta);

			if (i >= 16 && i < 48) {
				c += min.x;
				indices.add(4);
			} else {
				c += max.x;
				indices.add(5);
			}

			s = min.y - radius + s;

			vertices.add(c);
			vertices.add(s);
			if(FLOATS_PER_VERTEX == 8) {
				// add filler floats
				vertices.add(0.0f);
				vertices.add(0.0f);
				vertices.add(0.0f);
				vertices.add(0.0f);
			}
			vertices.add(0.0f);
			vertices.add(0.0f);

			int next = i + 1;
			next %= subdiv;

			indices.add(i + 6);
			indices.add(next + 6);

			theta += thetaStep;
		}

		float[] newMesh = new float[vertices.size()];
		for (int i = 0; i < vertices.size(); i++)
			newMesh[i] = vertices.get(i);
		int[] newIndices = new int[indices.size()];
		for (int i = 0; i < indices.size(); i++)
			newIndices[i] = indices.get(i);
		
		super.mesh = newMesh;
		super.indices = newIndices;
	}

}

