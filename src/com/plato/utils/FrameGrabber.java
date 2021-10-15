package com.plato.utils;

import java.io.ByteArrayInputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

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
    try (var out = new FileOutputStream("thumbnail.jpeg"); var in = new FileInputStream(
        "IMG_0010.MOV")) {
      var frameGrabber = new FrameGrabber(in);
      var result = frameGrabber.grabFrame();
      System.out.println("Read " + frameGrabber.currentlyReadBytes / 1024 + "KB");
      System.out.println("Size of thumbnail: "+ result.thumbnailBytes.length);
      out.write(result.thumbnailBytes);
      out.flush();
      System.out.println("Rotation: " + result.rotation);
    } catch (IOException e) {
      e.printStackTrace();
    }
  }

  private native Result grabFrame();

  public int readBytes(byte[] buffer) throws IOException {
    var nbRead = in.readNBytes(buffer, 0, buffer.length);
    currentlyReadBytes += nbRead;
    return nbRead;
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
