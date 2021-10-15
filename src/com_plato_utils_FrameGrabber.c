#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frame_grabber.h"
#include "com_plato_utils_FrameGrabber.h"

JNIEnv *gEnv = NULL;
jobject gThis;

static int read_n_bytes(uint8_t *buff, size_t buff_size)
{
  // Convert byte buffer to Java byte array
  jbyteArray byteArray = (*gEnv)->NewByteArray(gEnv, buff_size);

  // Find and invoke Java read method
  jclass thisClass = (*gEnv)->GetObjectClass(gEnv, gThis);
  jmethodID methodId = (*gEnv)->GetMethodID(gEnv, thisClass, "readBytes", "([B)I");
  jint result = (*gEnv)->CallIntMethod(gEnv, gThis, methodId, byteArray);

  /* get the body of array; it will be referecende by C pointer */
  jsize len = (*gEnv)->GetArrayLength(gEnv, byteArray);
  jboolean *body = (*gEnv)->GetBooleanArrayElements(gEnv, byteArray, 0);
  memcpy(buff, body, len);

  /* release body when you decide it is no longer needed */
  (*gEnv)->ReleaseBooleanArrayElements(gEnv, byteArray, body, 0);

  return result;
}

JNIEXPORT jbyteArray JNICALL Java_com_plato_utils_FrameGrabber_grabFrame(JNIEnv *env, jobject thisObject)
{
  gEnv = env;
  gThis = thisObject;

  uint8_t *thumbnail_bytes = NULL;
  size_t thumbnail_size;
  char *rotation = NULL;

  ResponseStatus response = grab_frame_from_input_stream(&read_n_bytes,
                                                         &thumbnail_bytes,
                                                         &thumbnail_size,
                                                         &rotation);

  gEnv = NULL;
  gThis = NULL;

  jclass jResultClass = (*env)->FindClass(env, "com/plato/utils/FrameGrabber$Result");

  if (response.code != FG_OK)
  {
    char exception_buffer[1024];
    sprintf(exception_buffer, "%s\n", response.description);
    jclass jexception_class = (*env)->FindClass(env, "com/plato/utils/FrameGrabberException");
    (*env)->ThrowNew(env,
                     jexception_class,
                     exception_buffer);
    jmethodID constructor = (*env)->GetMethodID(env, jResultClass, "<init>", "()V");
    jobject result = (*env)->NewObject(env, jResultClass, constructor);
    return result;
  }

  /* Thumbnail byte array */
  jbyteArray jbyte_array = (*env)->NewByteArray(env, thumbnail_size);
  (*env)->SetByteArrayRegion(env, jbyte_array, 0, thumbnail_size, thumbnail_bytes);

  /* String instance of rotation */
  jstring jrotation = (*env)->NewStringUTF(env, rotation ? rotation : "");

  /* Create result */
  jmethodID constructor = (*env)->GetMethodID(env, jResultClass, "<init>", "([BLjava/lang/String;)V");
  jobject result = (*env)->NewObject(env, jResultClass, constructor, jbyte_array, jrotation);

  free(thumbnail_bytes);
  if (rotation)
    free(rotation);

  return result;
}