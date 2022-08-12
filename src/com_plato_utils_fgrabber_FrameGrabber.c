#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frame_grabber.h"
#include "com_plato_utils_fgrabber_FrameGrabber.h"

JNIEnv *gEnv = NULL;
jobject gThis;

static int read_n_bytes(uint8_t *buff, size_t buff_size)
{
  // Convert byte buffer to Java byte array
  jbyteArray byteArray = (*gEnv)->NewByteArray(gEnv, buff_size);

  // Find and invoke Java read method
  jclass thisClass = (*gEnv)->GetObjectClass(gEnv, gThis);
  jmethodID methodId = (*gEnv)->GetMethodID(gEnv, thisClass, "readNBytes", "([B)I");
  jint result = (*gEnv)->CallIntMethod(gEnv, gThis, methodId, byteArray);

  /* get the body of array; it will be referecende by C pointer */
  jsize len = (*gEnv)->GetArrayLength(gEnv, byteArray);
  jbyte *body = (*gEnv)->GetByteArrayElements(gEnv, byteArray, 0);
  memcpy(buff, body, len);

  /* release body when you decide it is no longer needed */
  (*gEnv)->ReleaseByteArrayElements(gEnv, byteArray, body, 0);

  return result;
}

typedef struct ByteArray
{
  int8_t *buffer;
  size_t length
} ByteArray;

static ByteArray read_all_bytes()
{
  ByteArray *byteArray = NULL;
  // Find and invoke Java read method
  jclass thisClass = (*gEnv)->GetObjectClass(gEnv, gThis);
  jmethodID methodId = (*gEnv)->GetMethodID(gEnv, thisClass, "readAllBytes", "()[B");
  jbyteArray jByteArray = (*gEnv)->CallObjectMethod(gEnv, gThis, methodId);

  jsize len = (*gEnv)->GetArrayLength(gEnv, jByteArray);
  jbyte *body = (*gEnv)->GetByteArrayElements(gEnv, jByteArray, 0);

  byteArray = malloc(sizeof *byteArray);
  byteArray->buffer = (int8_t *)malloc(len * sizeof(int8_t));
  memcpy(byteArray->buffer, body, len);
  byteArray->length = len;

  /* release body when you decide it is no longer needed */
  (*gEnv)->ReleaseByteArrayElements(gEnv, jByteArray, body, 0);

  return *byteArray;
}

JNIEXPORT jobject JNICALL Java_com_plato_utils_fgrabber_FrameGrabber_grabFrame(JNIEnv *env, jobject thisObject)
{
  gEnv = env;
  gThis = thisObject;

  uint8_t *thumbnail_bytes = NULL;
  size_t thumbnail_size;
  char *rotation = NULL;

  ByteArray byteArray = read_all_bytes();

  ResponseStatus response = grab_frame_from_byte_buffer(byteArray.buffer, byteArray.length,
                                                        &thumbnail_bytes, &thumbnail_size,
                                                        &rotation);

  gEnv = NULL;
  gThis = NULL;

  jclass jResultClass = (*env)->FindClass(env, "com/plato/utils/fgrabber/FrameGrabber$Result");

  if (response.code != FG_OK)
  {
    char exception_buffer[1024];
    sprintf(exception_buffer, "%s\n", response.description);
    jclass jexception_class = (*env)->FindClass(env, "com/plato/utils/fgrabber/FrameGrabberException");
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
  free(byteArray.buffer);

  return result;
}