package ui;

//all labels implement comparator to facilitate depth sorting
public abstract class Label {
	 int depth;

	// basically a kind of layer differentiator
	// so different things can be drawn on top of each other
	public abstract int getDepth();

	// renderer so window can render each label independently (based on its own
	// configuration)
	public abstract void render();

	// universal cleanup method
	public abstract void cleanup();
	
	// universal visibility method
	public abstract boolean draw();

	// method for scripting
	// purposely leave empty for anonymous classes to override and implement
	public void update() {

	}

	// another method for scripting (ran at constructor end)
	// purposely leave empty for anonymous classes to override and implement
	public void init() {

	}
	
	//resize update method
	// purposely leave empty for anonymouse classes to override and implement
	public void resizeUpdate(int ow, int oh, int w, int h) {
		
	}
	
	//clicked method
	// purposely leave empty for anonymous classes to override and implement
	public void clicked(int x, int y, int button, int action) {
		
	}
}
