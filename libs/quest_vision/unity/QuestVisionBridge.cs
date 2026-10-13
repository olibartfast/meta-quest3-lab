// Unity (C#) marshalling sample for quest_vision.so / quest_vision.dll.
// Drop this file into Assets/Scripts. The native plugin must be placed in
// Assets/Plugins/Android/arm64-v8a/quest_vision.so (Quest) or
// Assets/Plugins/x86_64/quest_vision.dll (Editor/Windows).
// Every struct below is blittable and matches
// libs/quest_vision/include/quest_vision/quest_vision_c_api.h field order.

using System;
using System.Runtime.InteropServices;
using UnityEngine;

public static class QuestVisionBridge
{
    const string NativeLib = "quest_vision";

    [StructLayout(LayoutKind.Sequential)]
    public struct Vec3
    {
        public float x, y, z;
        public Vec3(Vector3 v) { x = v.x; y = v.y; z = v.z; }
        public Vector3 ToUnity() { return new Vector3(x, y, z); }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Quat
    {
        public float x, y, z, w;
        public Quaternion ToUnity() { return new Quaternion(x, y, z, w); }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Frustum
    {
        public Vec3 origin;
        // Fixed buffers keep the struct blittable for P/Invoke.
        public unsafe fixed float corners[12];
        public Vec3 centerDir;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FusionParams
    {
        public float borderInsetFraction;
        public float foregroundSampleShare;
        public float clusterToleranceMeters;
        public float extentLowPercentile;
        public float extentHighPercentile;
        public uint ransacIterations;
        public float ransacInlierMeters;
        public float ransacMinimumInlierRatio;
        public int requireMeasuredOrientation;
        public uint minimumSampleCount;
        public float minimumInlierRatio;
        public float minimumRangeMeters;
        public float maximumRangeMeters;
        public Vec3 up;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SizePrior
    {
        public int valid;
        public float depthMeters;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FusedBox
    {
        public Vec3 center;
        public Vec3 halfExtents;
        public Quat orientation;
        public float rangeMeters;
        public int depthAssumed;
        public int orientationMeasured;
        public float planeInlierRatio;
        public uint sampleCount;
        public float inlierRatio;
        public float confidence;
        public int rangeSource;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FuseResult
    {
        public FusedBox box;
        // Column-major 4x4: elements [c*4+r]. Copy column by column
        // into a Unity Matrix4x4 (SetColumn).
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public float[] poseMatrix;
    }

    // Opaque session handle. Create once, fuse per frame, destroy on quit.
    // Mirrors PoseTrack's posetrack_create/process/destroy shape.
    [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr QvCreate();

    [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
    public static extern void QvDestroy(IntPtr handle);

    [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int QvSetParams(IntPtr handle, ref FusionParams @params);

    // 0 = fused, 1 = refused (see reason), <0 = usage error.
    [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int QvFuse(
        IntPtr handle,
        ref Frustum frustum,
        [In] Vec3[] points,
        int pointCount,
        ref SizePrior prior,
        out FuseResult result,
        [Out] byte[] reason,
        int reasonSize);

    [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr QvGetLastError(IntPtr handle);

    [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
    public static extern void QvFusionParamsDefault(ref FusionParams @params);

    [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr QvVersion();

    public static string Version()
    {
        return Marshal.PtrToStringAnsi(QvVersion());
    }

    // Example MonoBehaviour wiring: session lifecycle plus per-frame fuse.
    // Attach to a GameObject, assign a cuboid prefab, feed detector/depth.
    public class QuestVisionTracker : MonoBehaviour
    {
        public GameObject cuboidPrefab;
        public float classDepthPriorMeters = -1f;
        IntPtr session = IntPtr.Zero;
        GameObject cuboid;

        void Start()
        {
            session = QvCreate();
            if (session == IntPtr.Zero)
                Debug.LogError("QuestVision: native session failed");
        }

        void OnDestroy()
        {
            if (session != IntPtr.Zero) { QvDestroy(session); session = IntPtr.Zero; }
            if (cuboid != null) Destroy(cuboid);
        }

        // Returns true and poses the cuboid when depth supports it.
        public bool TryFuse(
            Vector3 originLocal,
            Vector3[] cornerDirsLocal, // length 4
            Vector3 centerDirLocal,
            Vector3[] depthPointsLocal)
        {
            if (session == IntPtr.Zero) return false;
            var frustum = new Frustum
            {
                origin = new Vec3(originLocal),
                centerDir = new Vec3(centerDirLocal),
            };
            unsafe
            {
                for (int i = 0; i < 4; i++)
                {
                    frustum.corners[i * 3 + 0] = cornerDirsLocal[i].x;
                    frustum.corners[i * 3 + 1] = cornerDirsLocal[i].y;
                    frustum.corners[i * 3 + 2] = cornerDirsLocal[i].z;
                }
            }
            var points = new Vec3[depthPointsLocal.Length];
            for (int i = 0; i < points.Length; i++)
                points[i] = new Vec3(depthPointsLocal[i]);
            var prior = new SizePrior
            {
                valid = classDepthPriorMeters > 0f ? 1 : 0,
                depthMeters = classDepthPriorMeters,
            };
            var reason = new byte[256];
            int rc = QvFuse(
                session, ref frustum, points, points.Length, ref prior,
                out FuseResult result, reason, reason.Length);
            if (rc != 0)
            {
                string why = System.Text.Encoding.ASCII.GetString(reason)
                    .TrimEnd('\0');
                Debug.Log($"QuestVision refused ({rc}): {why}");
                if (cuboid != null) cuboid.SetActive(false);
                return false;
            }
            // Pose straight from the native 4x4: no Unity-side math.
            var m = new Matrix4x4();
            for (int c = 0; c < 4; c++)
                m.SetColumn(c, new Vector4(
                    result.poseMatrix[c * 4 + 0], result.poseMatrix[c * 4 + 1],
                    result.poseMatrix[c * 4 + 2], result.poseMatrix[c * 4 + 3]));
            if (cuboid == null) cuboid = Instantiate(cuboidPrefab);
            cuboid.SetActive(true);
            cuboid.transform.position = m.GetColumn(3);
            cuboid.transform.rotation = m.rotation;
            cuboid.transform.localScale = result.box.halfExtents.ToUnity() * 2f;
            return true;
        }
    }
}
