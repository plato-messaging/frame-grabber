package com.plato.utils.fgrabber;

import java.io.ByteArrayInputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class FrameGrabber {

  static {
      System.loadLibrary("fgrabber");
  }

  private final InputStream in;
  private int currentlyReadBytes = 0;

  private FrameGrabber(byte[] bytes) {
    in = new ByteArrayInputStream(bytes);
  }

  private FrameGrabber(InputStream in) {
    this.in = in;
  }

  public static void main(String[] args) {
    try (var out = new FileOutputStream("thumbnail.jpeg"); var in = new FileInputStream("897.mp4")) {
      var frameGrabber = new FrameGrabber(in);
      var result = frameGrabber.grabFrame();
      System.out.println("Read " + frameGrabber.currentlyReadBytes / 1024 + "KB");
      System.out.println("Size of thumbnail: " + result.thumbnailBytes.length);
      out.write(result.thumbnailBytes);
      out.flush();
      System.out.println("Rotation: " + result.rotation);
    } catch (IOException e) {
      e.printStackTrace();
    }
  }

  public native Result grabFrame();

  public int readNBytes(byte[] buffer) throws IOException {
    var nbRead = in.readNBytes(buffer, 0, buffer.length);
    currentlyReadBytes += nbRead;
    return nbRead;
  }

  public byte[] readAllBytes() {
    try {
      var bytes = in.readAllBytes();
      currentlyReadBytes = bytes.length;
      return bytes;
    } catch (Exception e) {
      return new byte[] {};
    }
  }

  public static class Result {
    private final byte[] thumbnailBytes;
    private final String rotation;

    public Result(byte[] thumbnailBytes, String rotation) {
      this.thumbnailBytes = thumbnailBytes;
      this.rotation = rotation;
    }

    public Result() {
      this.thumbnailBytes = new byte[0];
      this.rotation = "";
    }
  }
}
