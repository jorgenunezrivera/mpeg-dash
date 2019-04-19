package ffmpeg_jni;

public class VideoDash {
	static {
		String property = System.getProperty("java.library.path");
		System.setProperty("java.library.path",property + ":/usr/local/lib");			
		//System.loadLibrary("VideoThumbnail");
		System.load("/home/brad/ServidorStreamingMpegDash/ffmpeg_jni/jni/libVideoDash.so");//provisional para no instalar cada vez
   }
private native int getVideoDash(String filename); 

	public static int videoDash(String filename) {
		int result = new VideoDash().getVideoDash(filename);
		return result;
}
	
	public static void main(String args[]) {	    		
		int result=videoDash("/home/brad/Descargas/Mandelbrot.mp4");
		System.out.println("result: "+result);
	}
}
