/*
 * Copyright 2014 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdlib>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cassert>

#include <cmath>

#include "TangoHandler.h"

#include <sstream>

#include <thread>

#include "glm.hpp"
#include "gtc/type_ptr.hpp"
#include "gtx/transform.hpp"
#include "gtx/quaternion.hpp"

using namespace glm;

namespace {

const int kVersionStringLength = 128;
// The minimum Tango Core version required from this application.
const int kTangoCoreMinimumVersion = 9377;

const float ANDROID_WEBVIEW_ADDRESS_BAR_HEIGHT = 125;

void onTextureAvailable(void* context, TangoCameraId tangoCameraId)
{
  // Do nothing for now.
}

void onTangoEventAvailable(void* context, const TangoEvent* event) {
  tango_chromium::TangoHandler* tangoHandler =
      static_cast<tango_chromium::TangoHandler*>(context);
  tangoHandler->onTangoEventAvailable(event);
}

void matrixFrustum(float const & left,
             float const & right,
             float const & bottom,
             float const & top,
             float const & near,
             float const & far,
             float* matrix)
{

  float x = 2 * near / ( right - left );
  float y = 2 * near / ( top - bottom );

  float a = ( right + left ) / ( right - left );
  float b = ( top + bottom ) / ( top - bottom );
  float c = - ( far + near ) / ( far - near );
  float d = - 2 * far * near / ( far - near );

  matrix[ 0 ] = x;  matrix[ 4 ] = 0;  matrix[ 8 ] = a;  matrix[ 12 ] = 0;
  matrix[ 1 ] = 0;  matrix[ 5 ] = y;  matrix[ 9 ] = b;  matrix[ 13 ] = 0;
  matrix[ 2 ] = 0;  matrix[ 6 ] = 0;  matrix[ 10 ] = c; matrix[ 14 ] = d;
  matrix[ 3 ] = 0;  matrix[ 7 ] = 0;  matrix[ 11 ] = - 1; matrix[ 15 ] = 0;


    // matrix[0] = (float(2) * near) / (right - left);
    // matrix[5] = (float(2) * near) / (top - bottom);
    // matrix[2][0] = (right + left) / (right - left);
    // matrix[2][1] = (top + bottom) / (top - bottom);
    // matrix[10] = -(farVal + nearVal) / (farVal - nearVal);
    // matrix[2][3] = float(-1);
    // matrix[3][2] = -(float(2) * farVal * nearVal) / (farVal - nearVal);
}

void matrixProjection(float width, float height,
                      float fx, float fy,
                      float cx, float cy,
                      float near, float far,
                      float* matrix)
{
  const float xscale = near / fx;
  const float yscale = near / fy;

  const float xoffset = (cx - (width / 2.0)) * xscale;
  // Color camera's coordinates has y pointing downwards so we negate this term.
  const float yoffset = -(cy - (height / 2.0)) * yscale;

  matrixFrustum(xscale * -width / 2.0f - xoffset,
          xscale * width / 2.0f - xoffset,
          yscale * -height / 2.0f - yoffset,
          yscale * height / 2.0f - yoffset, near, far,
          matrix);
}

mat4 SS_T_GL;
mat4 SS_T_GL_INV;

mat4 mat4FromTranslationOrientation(double *translation, double *orientation) {
  vec3 translationV3 = vec3((float)translation[0],
  (float)translation[1], (float)translation[2]);
  quat orientationQ = quat((float)orientation[0], (float)orientation[1],
  (float)orientation[2], (float)orientation[3]);

  mat4 m = mat4();
  float* out;
  out = value_ptr(m);
  float x = (float)orientation[0];
  float y = (float)orientation[1];
  float z = (float)orientation[2];
  float w = (float)orientation[3];

  float x2 = x + x;
  float y2 = y + y;
  float z2 = z + z;
  float xx = x * x2;
  float xy = x * y2;
  float xz = x * z2;
  float yy = y * y2;
  float yz = y * z2;
  float zz = z * z2;
  float wx = w * x2;
  float wy = w * y2;
  float wz = w * z2;

  out[0] = 1 - (yy + zz);
  out[1] = xy + wz;
  out[2] = xz - wy;
  out[3] = 0;
  out[4] = xy - wz;
  out[5] = 1 - (xx + zz);
  out[6] = yz + wx;
  out[7] = 0;
  out[8] = xz + wy;
  out[9] = yz - wx;
  out[10] = 1 - (xx + yy);
  out[11] = 0;
  out[12] = (float)translation[0];
  out[13] = (float)translation[1];
  out[14] = (float)translation[2];
  out[15] = 1;

  return m;
};

float rayIntersectsPlane(const vec3 &planeNormal, const vec3 &planePosition,
    const vec3 &rayOrigin, const vec3 &rayDirection) {
  float denom = glm::dot(planeNormal, rayDirection);
  vec3 rayToPlane = planePosition - rayOrigin;
  return glm::dot(rayToPlane, planeNormal) / denom;
}

vec3 transformVec3ByMat4(const vec3 &v, const mat4 &m4) {
  const float* m;
  m = value_ptr(m4);

  float x = v[0];
  float y = v[1];
  float z = v[2];
  float w = m[3] * x + m[7] * y + m[11] * z + m[15];

  if (w == 0) {
    w = 1.0f;
  }

  return vec3(
    (m[0] * x + m[4] * y + m[8] * z + m[12]) / w,
    (m[1] * x + m[5] * y + m[9] * z + m[13]) / w,
    (m[2] * x + m[6] * y + m[10] * z + m[14]) / w);
}

void setFloat16FromMat4(float *out, const mat4 &m4) {
  const float* fM = value_ptr(m4);
  for (int i = 0; i < 16; i++) {
    out[i] = fM[i];
  }
}

bool isPointInPolygon(const vec3 &point, const vec3* polygonPoints, int count) {
  if (count < 3)
  {
    return false;
  }

  LOGE("POINT: (%f, %f, %f)", point.x, point.y, point.z);
  LOGE("POLY");
  vec3 lastUp = vec3(0, 0, 0);
  for (int i = 0; i < count; ++i)
  {
    LOGE("(%f, %f, %f", polygonPoints[i].x, polygonPoints[i].y, polygonPoints[i].z);
    vec3 v0 = point - polygonPoints[i];
    vec3 v1;
    if (i == count - 1)
    {
      v1 = polygonPoints[0] - polygonPoints[i];
    }
    else
    {
      v1 = polygonPoints[i + 1] - polygonPoints[i];
    }

    vec3 up = glm::cross(v0, v1);
    if (i != 0)
    {
      float sign = glm::dot(up, lastUp);
      if (sign < 0)
      {
        return false;
      }
    }

    lastUp = up;
  }
  LOGE("/POLY");
  return true;
}

} // End anonymous namespace

namespace tango_chromium {

TangoHandler* TangoHandler::instance = 0;

TangoHandler* TangoHandler::getInstance()
{
  if (instance == 0)
  {
    instance = new TangoHandler();
  }
  return instance;
}

void TangoHandler::releaseInstance()
{
  delete instance;
  instance = 0;
}

TangoHandler::TangoHandler(): connected(false)
  , tangoConfig(nullptr)
  , lastTangoImageBufferTimestamp(0)
  , cameraImageWidth(0)
  , cameraImageHeight(0)
  , cameraImageTextureWidth(0)
  , cameraImageTextureHeight(0)
  , textureIdConnected(false)
{
  // SS means Start of Service: this matrix does the conversion from Start of Service transform
  // to OpenGL (tango-space has z as the up-vector, not y).
  float ss_t_gl[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1};
  SS_T_GL = make_mat4(ss_t_gl);
  SS_T_GL_INV = inverse(SS_T_GL);
}

TangoHandler::~TangoHandler()
{
  TangoConfig_free(tangoConfig);
  tangoConfig = nullptr;
}

void TangoHandler::onCreate(JNIEnv* env, jobject activity, int activityOrientation, int sensorOrientation)
{
  this->activityOrientation = activityOrientation;
  this->sensorOrientation = sensorOrientation;
}

void TangoHandler::onTangoServiceConnected(JNIEnv* env, jobject tango)
{
  TangoService_CacheTangoObject(env, tango);

  connect();
}


void TangoHandler::connect()
{
  TangoErrorType result;

  // TANGO_CONFIG_DEFAULT is enabling Motion Tracking and disabling Depth
  // Perception.
  tangoConfig = TangoService_getConfig(TANGO_CONFIG_DEFAULT);
  if (tangoConfig == nullptr)
  {
    LOGE("TangoHandler::connect, TangoService_getConfig error.");
    std::exit (EXIT_SUCCESS);
  }

  // Set auto-recovery for motion tracking as requested by the user.
  result = TangoConfig_setBool(tangoConfig, "config_enable_auto_recovery", true);
  if (result != TANGO_SUCCESS) {
    LOGE("TangoHandler::connect, config_enable_auto_recovery activation failed with error code: %d", result);
    std::exit(EXIT_SUCCESS);
  }

  // Enable color camera from config.
  result = TangoConfig_setBool(tangoConfig, "config_enable_color_camera", true);
  if (result != TANGO_SUCCESS) {
    LOGE("TangoHandler::connect, config_enable_color_camera() failed with error code: %d", result);
    std::exit(EXIT_SUCCESS);
  }

  // Note that it is super important for AR applications that we enable low
  // latency IMU integration so that we have pose information available as
  // quickly as possible. Without setting this flag, you will often receive
  // invalid poses when calling getPoseAtTime() for an image.
  result = TangoConfig_setBool(tangoConfig, "config_enable_low_latency_imu_integration", true);
  if (result != TANGO_SUCCESS) {
    LOGE("TangoHandler::connect, failed to enable low latency imu integration.");
    std::exit(EXIT_SUCCESS);
  }

  // Drift correction allows motion tracking to recover after it loses tracking.
  // The drift corrected pose is is available through the frame pair with
  // base frame AREA_DESCRIPTION and target frame DEVICE.
  result = TangoConfig_setBool(tangoConfig, "config_enable_drift_correction", true);
  if (result != TANGO_SUCCESS) {
    LOGE("TangoHandler::connect, enabling config_enable_drift_correction "
      "failed with error code: %d", result);
    std::exit(EXIT_SUCCESS);
  }

  // Enable Depth Perception.
  result = TangoConfig_setBool(tangoConfig, "config_enable_depth", true);
  if (result != TANGO_SUCCESS)
  {
    LOGE("TangoHandler::connect, config_enable_depth activation failed with error code: %d.", result);
    std::exit(EXIT_SUCCESS);
  }

  // Enabling experimental plane finding.
  result = TangoConfig_setBool(tangoConfig,
                            "config_experimental_enable_plane_detection", true);
  if (result != TANGO_SUCCESS) {
    LOGE("TangoHandler::connect, failed to enable experimental plane finding.");
    std::exit(EXIT_SUCCESS);
  }

  result = TangoConfig_setInt32(tangoConfig, "config_depth_mode",
                                TANGO_POINTCLOUD_XYZC);
  if (result != TANGO_SUCCESS) {
    LOGE(
        "TangoHandler::connect, failed to configure point cloud to "
        "XYZC.");
    std::exit(EXIT_SUCCESS);
  }

  // Get TangoCore version string from service.
  char tangoCoreVersionCharArray[kVersionStringLength];
  result = TangoConfig_getString(tangoConfig, "tango_service_library_version",
                              tangoCoreVersionCharArray, kVersionStringLength);
  if (result != TANGO_SUCCESS) {
    LOGE(
        "TangoHandler::connect, get tango core version failed with error"
        "code: %d",
        result);
    std::exit(EXIT_SUCCESS);
  }
  tangoCoreVersionString = tangoCoreVersionCharArray;

  // Connect some callbacks
  result = TangoService_connectOnTextureAvailable(TANGO_CAMERA_COLOR, this, ::onTextureAvailable);
  if (result != TANGO_SUCCESS)
  {
    LOGE("TangoHandler::connect, failed to connect texture callback with error code: %d", result);
    std::exit(EXIT_SUCCESS);
  }

  // Attach onEventAvailable callback.
  // The callback will be called after the service is connected.
  result = TangoService_connectOnTangoEvent(::onTangoEventAvailable);
  if (result != TANGO_SUCCESS) {
    LOGE(
        "TangoHandler::connect, failed to connect to event callback with error"
        "code: %d",
        result);
    std::exit(EXIT_SUCCESS);
  }

  // Connect the tango service.
  if (TangoService_connect(this, tangoConfig) != TANGO_SUCCESS)
  {
    LOGE("TangoHandler::connect, TangoService_connect error.");
    std::exit (EXIT_SUCCESS);
  }

  // Get the intrinsics for the color camera and pass them on to the depth
  // image.
  result = TangoService_getCameraIntrinsics(TANGO_CAMERA_COLOR, &tangoCameraIntrinsics);
  if (result != TANGO_SUCCESS) {
    LOGE("TangoHandler::connect: Failed to get the intrinsics for the color camera.");
    std::exit(EXIT_SUCCESS);
  }

  // Initialize TangoSupport context.
  TangoSupport_initialize(TangoService_getPoseAtTime,
                          TangoService_getCameraIntrinsics);

  connected = true;

  // Update camera intrinsics after connection which will take into account
  // device rotation
  this->updateCameraIntrinsics();
}

void TangoHandler::disconnect()
{
  TangoService_disconnect();

  cameraImageWidth = cameraImageHeight =
    cameraImageTextureWidth = cameraImageTextureHeight = 0;

  textureIdConnected = false;

  connected = false;
}

void TangoHandler::onPause()
{
  disconnect();
}

void TangoHandler::onDeviceRotationChanged(int activityOrientation, int sensorOrientation)
{
  LOGE("TangoHandler::onDeviceRotationChanged; activityOrientation=%d, sensorOrientation=%d",
    activityOrientation, sensorOrientation);
  this->activityOrientation = activityOrientation;
  this->sensorOrientation = sensorOrientation;
  this->updateCameraIntrinsics();
}

void TangoHandler::onTangoEventAvailable(const TangoEvent* event)
{
}

bool TangoHandler::isConnected() const
{
  return connected;
}

bool TangoHandler::getPose(TangoPoseData* tangoPoseData)
{
  bool result = connected;
  if (connected)
  {
    double timestamp = hasLastTangoImageBufferTimestampChangedLately() ? lastTangoImageBufferTimestamp : 0.0;

    // LOGI("JUDAX: TangoHandler::getPose timestamp = %lf", timestamp);

// For reference purpose: ADF to start of service request parameters.
// {TANGO_COORDINATE_FRAME_AREA_DESCRIPTION,
//         TANGO_COORDINATE_FRAME_DEVICE}
// then fallback to:
// {TANGO_COORDINATE_FRAME_START_OF_SERVICE,
//         TANGO_COORDINATE_FRAME_DEVICE}

    result = TangoSupport_getPoseAtTime(
      timestamp, TANGO_COORDINATE_FRAME_START_OF_SERVICE,
      TANGO_COORDINATE_FRAME_CAMERA_COLOR, TANGO_SUPPORT_ENGINE_OPENGL,
      TANGO_SUPPORT_ENGINE_OPENGL,
      static_cast<TangoSupport_Rotation>(activityOrientation), tangoPoseData) == TANGO_SUCCESS;
    if (!result)
    {
      LOGE("TangoHandler::getPose: Failed to get the pose.");
    }

  }
  return result;
}

bool TangoHandler::getProjectionMatrix(float near, float far, float* projectionMatrix)
{
  if (!connected) return false;

  bool result = this->updateCameraIntrinsics();

  if (!result) {
    LOGE(
      "TangoHandler::getProjectionMatrix, failed to get camera intrinsics");
    return false;
  }

  float image_width = static_cast<float>(tangoCameraIntrinsics.width);
  float image_height = static_cast<float>(tangoCameraIntrinsics.height);
  float fx = static_cast<float>(tangoCameraIntrinsics.fx);
  float fy = static_cast<float>(tangoCameraIntrinsics.fy);
  float cx = static_cast<float>(tangoCameraIntrinsics.cx);
  float cy = static_cast<float>(tangoCameraIntrinsics.cy);

  matrixProjection(
    image_width, image_height, fx, fy, cx, cy, near,
    far, projectionMatrix);

  return true;
}

bool TangoHandler::hitTest(float x, float y, std::vector<Hit>& hits)
{
  bool result = false;

  if (connected)
  {
    TangoPlaneData* planes = 0;
    size_t numberOfPlanes = 0;
    if (TangoService_Experimental_getPlanes(&planes, &numberOfPlanes) == TANGO_SUCCESS)
    {
      if (numberOfPlanes == 0) {
        return result;
      }

      // Set the projection matrix.
      float* fM;
      mat4 projectionMatrix;
      fM = value_ptr(projectionMatrix);
      // TODO(lincolnfrog): near and far values should not be constants. Use the values
      // supplied by the client.
      getProjectionMatrix(0.01, 10000, fM);

      // Set the model view matrix.
      TangoPoseData poseData;
      getPose(&poseData);
      mat4 viewMatrix = mat4FromTranslationOrientation(
          poseData.translation, poseData.orientation);
      viewMatrix = inverse(viewMatrix);

      // Combine the projection and model view matrices.
      mat4 projViewMatrix = projectionMatrix * viewMatrix;

      // Invert the combined matrix because we need to go from screen -> world.
      projViewMatrix = inverse(projViewMatrix);

      // Create a ray in screen-space for the hit test ([-1, 1] with y flip).
      vec3 rayStart = vec3((2 * x) - 1, (2 * (1 - y)) - 1, 0);
      vec3 rayEnd   = vec3((2 * x) - 1, (2 * (1 - y)) - 1, 1);

      // Transform the ray into world-space.
      vec3 worldRayOrigin = transformVec3ByMat4(rayStart, projViewMatrix);
      vec3 worldRayDirection = transformVec3ByMat4(rayEnd, projViewMatrix);
      worldRayDirection -= worldRayOrigin;
      worldRayDirection = normalize(worldRayDirection);

      // Check each plane for intersections.
      for (int i = 0; i < numberOfPlanes; i++) {
        TangoPlaneData planeData = planes[i];
        if (!planeData.is_valid) {
          // This plane is no longer valid, so skip it.
          continue;
        }
        if (planeData.subsumed_by != -1) {
          // This plane has been subsumed by another plane, so skip it.
          continue;
        }

        TangoPoseData planePose = planeData.pose;

        // Get the plane transform matrix.
        mat4 planeMatrix = mat4FromTranslationOrientation(
            planePose.translation, planePose.orientation);

        // Plane is in the tango coordinates, so transform into GL coordinate space.
        planeMatrix = SS_T_GL_INV * planeMatrix * SS_T_GL;

        // Get the plane center in world-space.
        vec3 planeCenter = vec3(planeData.center_x, 0, planeData.center_y);
        vec3 planePosition = transformVec3ByMat4(planeCenter, planeMatrix);

        // Assume all planes are oriented horizontally.
        vec3 planeNormal = vec3(0, 1, 0);

        // Check if the ray intersects the plane.
        float t = rayIntersectsPlane(planeNormal, planePosition, worldRayOrigin, worldRayDirection);

        // If t < 0, there is no intersection.
        if (t < 0) {
          continue;
        }

        // Calculate the intersection point.
        vec3 planeIntersection = worldRayOrigin + (worldRayDirection * t);

        // Convert the intersection into plane-space.
        mat4 planeMatrixInv = inverse(planeMatrix);
        vec3 planeIntersectionLocal = transformVec3ByMat4(planeIntersection, planeMatrixInv);

        // Check if the intersection is outside of the extent of the plane.
        if (abs(planeIntersectionLocal.x - planeData.center_x) > planeData.width) {
          continue;
        }
        if (abs(planeIntersectionLocal.y - planeData.center_y) > planeData.height) {
          continue;
        }

        /*
        // TODO(lincolnfrog): enable polygon test when we figure out why the polygon is so bad.s
        // Transform all the points into world space using the plane matrix.
        vec3 polygonPoints[planeData.boundary_point_num];
        for (int i = 0; i < planeData.boundary_point_num; ++i) {
          polygonPoints[i] = transformVec3ByMat4(
            vec3((float)planeData.boundary_polygon[i * 2], 0, (float)planeData.boundary_polygon[(i * 2) + 1]), 
            planeMatrix);
        }
        
        if(!isPointInPolygon(planeIntersection, polygonPoints, planeData.boundary_point_num)) {
          LOGE("%i Rejected (POLY)", planeData.id);
          continue;
        }
        */

        mat4 hitMatrix = translate(mat4(1.0f), planeIntersection);
        Hit hit;
        setFloat16FromMat4(hit.modelMatrix, hitMatrix);
        hits.push_back(hit);
      }

      // Sort the hits based on distance to the camera.
      auto sortFunc = [worldRayOrigin] (Hit a, Hit b) {
        vec3 vA = vec3(a.modelMatrix[12], a.modelMatrix[13], a.modelMatrix[14]);
        float dA = glm::length2(vA - worldRayOrigin);

        vec3 vB = vec3(b.modelMatrix[12], b.modelMatrix[13], b.modelMatrix[14]);
        float dB = glm::length2(vB - worldRayOrigin);
        return dA < dB;
      };

      std::sort(hits.begin(), hits.end(), sortFunc);

      TangoPlaneData_free(planes, numberOfPlanes);
    }

    result = hits.size() > 0;
  }

  return result;
}

void TangoHandler::resetPose()
{
  TangoService_resetMotionTracking();
}

bool TangoHandler::updateCameraIntrinsics()
{
  if (!connected) {
    LOGE("TangoHandler::updateCameraIntrinsics, is not connected.");
    return false;
  }

  int result = TangoSupport_getCameraIntrinsicsBasedOnDisplayRotation(
      TANGO_CAMERA_COLOR, static_cast<TangoSupport_Rotation>(activityOrientation),
      &tangoCameraIntrinsics);

  if (result != TANGO_SUCCESS) {
    LOGE(
        "TangoHandler::updateCameraIntrinsics, failed to get camera intrinsics "
        "with error code: %d",
        result);
    return false;
  }

  /*
  LOGE("TangoHandler::updateCameraIntrinsics, success. fx: %f, fy: %f, width: %d, height: %d, cx: %f, cy: %f",
    tangoCameraIntrinsics.fx,
    tangoCameraIntrinsics.fy,
    tangoCameraIntrinsics.width,
    tangoCameraIntrinsics.height,
    tangoCameraIntrinsics.cx,
    tangoCameraIntrinsics.cy);
  */

  // Always subtract the height of the address bar since we cannot
  // get rid of it
  tangoCameraIntrinsics.height -= ANDROID_WEBVIEW_ADDRESS_BAR_HEIGHT;

  // Update the stored values for width and height
  cameraImageWidth = cameraImageTextureWidth = tangoCameraIntrinsics.width;
  cameraImageHeight = cameraImageTextureHeight = tangoCameraIntrinsics.height;

  return true;
}

bool TangoHandler::getCameraImageSize(uint32_t* width, uint32_t* height)
{
  bool result = true;

  *width = cameraImageWidth;
  *height = cameraImageHeight;

  return result;
}

bool TangoHandler::getCameraImageTextureSize(uint32_t* width, uint32_t* height)
{
  bool result = true;

  *width = cameraImageTextureWidth;
  *height = cameraImageTextureHeight;

  return result;
}

bool TangoHandler::getCameraFocalLength(double* focalLengthX, double* focalLengthY)
{
  bool result = true;
  *focalLengthX = tangoCameraIntrinsics.fx;
  *focalLengthY = tangoCameraIntrinsics.fy;
  return result;
}

bool TangoHandler::getCameraPoint(double* x, double* y)
{
  bool result = true;
  *x = tangoCameraIntrinsics.cx;
  *y = tangoCameraIntrinsics.cy;
  return result;
}

bool TangoHandler::updateCameraImageIntoTexture(uint32_t textureId)
{
  if (!connected) return false;

  TangoErrorType result = TangoService_updateTextureExternalOes(TANGO_CAMERA_COLOR, textureId, &lastTangoImageBufferTimestamp);

  std::time(&lastTangoImagebufferTimestampTime);

  // LOGI("JUDAX: TangoHandler::updateCameraImageIntoTexture lastTangoImageBufferTimestamp = %lf, result = %d, textureId = %d", lastTangoImageBufferTimestamp, result, textureId);

  return result == TANGO_SUCCESS;
}

int TangoHandler::getSensorOrientation() const
{
  return sensorOrientation;
}

int TangoHandler::getActivityOrientation() const
{
  return activityOrientation;
}

bool TangoHandler::hasLastTangoImageBufferTimestampChangedLately()
{
  std::time_t currentTime;
  std::time(&currentTime);
  return std::difftime(currentTime, lastTangoImagebufferTimestampTime) < 1.0;
}

}  // namespace tango_chromium
