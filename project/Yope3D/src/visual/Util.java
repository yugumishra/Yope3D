package visual;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.Scanner;

//this class contains utility methods
//that can't be contained anywhere else
//like file reading
public class Util {
	//reads a shader from a source file and returns it as a string
	public static String readShader(String source) {
		//open scanner
		Scanner scan = null;
		//with error caught
		try {
			scan = new Scanner(new File(source));
		} catch (FileNotFoundException e) {
			System.err.println("The file " + source + " could not be found");
		}
		//then read shader
		String shader = "";
		while(scan.hasNextLine()) {
			shader += scan.nextLine() + "\n";
		}
		//close
		scan.close();
		//return
		return shader;
	}
}
