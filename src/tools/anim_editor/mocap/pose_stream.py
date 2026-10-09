"""Webcam (or video) body tracking for the njin Animation Editor.

Runs MediaPipe Pose Landmarker (and optionally Hand Landmarker) on each frame
and sends the 3D world landmarks to the editor as one UDP packet per frame on
127.0.0.1. The editor starts this script itself (Mocap > Start); it can also be
run by hand, then the editor only listens.

  pose_stream.py [--camera 0] [--port 47800] [--hands] [--mirror] [--preview] [--preview-scale 0.5]
  pose_stream.py --video clip.mp4 [--dump out.bin]      # tests: a file instead
  pose_stream.py --frames folder [--fps 30] [--dump out.bin]

Packet (little-endian, version 1):
  char[4]  magic "NJMC"
  u16      version (1)
  u16      flags: bit0 pose found, bit1 left hand, bit2 right hand, bit3 mirrored
  f64      capture time, seconds (wall clock time.time() for a camera, media time for a file)
  u32      frame number
  f32[33*4] pose world landmarks: x, y, z (metres, origin between the hips,
           MediaPipe axes: x right in the image, y down, z away from the camera)
           and visibility (0..1)
  f32[21*3] left hand world landmarks (only when bit1), the person's left
  f32[21*3] right hand world landmarks (only when bit2)

Hands are given as the person's left/right, matched to the pose wrists in the
image, not MediaPipe's handedness label (which assumes a mirrored selfie image).
--mirror flips the image first, so the character moves like a mirror image.

MediaPipe is Apache-2.0 (https://github.com/google-ai-edge/mediapipe).
"""

import argparse
import os
import signal
import socket
import struct
import sys
import time

import cv2
import mediapipe as mp
from mediapipe.tasks.python import BaseOptions, vision

HERE = os.path.dirname(os.path.abspath(__file__))
MAGIC = b"NJMC"
VERSION = 1
POSE_EDGES = [(11, 12), (11, 13), (13, 15), (12, 14), (14, 16), (11, 23), (12, 24), (23, 24), (23, 25), (25, 27),
              (24, 26), (26, 28), (27, 31), (28, 32), (0, 7), (0, 8)]


def packet(frame_no, t, pose_world, vis, left, right, mirrored):
    flags = (1 if pose_world is not None else 0) | (2 if left is not None else 0) | (4 if right is not None else 0)
    flags |= 8 if mirrored else 0
    out = bytearray(MAGIC)
    out += struct.pack("<HHdI", VERSION, flags, t, frame_no)
    if pose_world is not None:
        for (x, y, z), v in zip(pose_world, vis):
            out += struct.pack("<4f", x, y, z, v)
    else:
        out += bytes(33 * 16)
    for hand in (left, right):
        if hand is not None:
            for x, y, z in hand:
                out += struct.pack("<3f", x, y, z)
    return bytes(out)


def frames_from(args):
    """Yields (bgr image, time in seconds)."""
    if args.frames:
        names = sorted(n for n in os.listdir(args.frames) if n.lower().endswith((".png", ".jpg", ".jpeg")))
        for i, name in enumerate(names):
            img = cv2.imread(os.path.join(args.frames, name))
            if img is not None:
                yield img, i / args.fps
        return
    cap = cv2.VideoCapture(args.video if args.video else args.camera, cv2.CAP_ANY if args.video else cv2.CAP_DSHOW)
    if not cap.isOpened() and not args.video:
        cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        sys.exit("cannot open " + (args.video or "camera %d" % args.camera))
    if not args.video:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
    i = 0
    file_fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    while True:
        ok, img = cap.read()
        if not ok:
            break
        yield img, (i / file_fps) if args.video else time.time()
        i += 1
    cap.release()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--camera", type=int, default=0)
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--video")
    ap.add_argument("--frames")
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--port", type=int, default=47800)
    ap.add_argument("--hands", action="store_true")
    ap.add_argument("--mirror", action="store_true")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--preview-scale", type=float, default=0.5,
                    help="size of the preview window per side (0.5 = a quarter of the image area)")
    ap.add_argument("--dump", help="also append every packet to this file (tests)")
    ap.add_argument("--model", default=os.path.join(HERE, "pose_landmarker_full.task"))
    args = ap.parse_args()

    stop = []
    signal.signal(signal.SIGINT, lambda *_: stop.append(1))
    if not os.path.exists(args.model):
        sys.exit("missing %s: run setup.bat" % args.model)
    pose = vision.PoseLandmarker.create_from_options(vision.PoseLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=args.model), running_mode=vision.RunningMode.VIDEO,
        num_poses=1, min_pose_detection_confidence=0.5, min_tracking_confidence=0.5))
    hands = None
    if args.hands:
        hand_model = os.path.join(HERE, "hand_landmarker.task")
        if not os.path.exists(hand_model):
            sys.exit("missing %s: run setup.bat" % hand_model)
        hands = vision.HandLandmarker.create_from_options(vision.HandLandmarkerOptions(
            base_options=BaseOptions(model_asset_path=hand_model), running_mode=vision.RunningMode.VIDEO,
            num_hands=2))

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dump = open(args.dump, "wb") if args.dump else None
    last_ms = -1
    frame_no = 0
    shown = time.monotonic()
    t0 = None
    for img, t in frames_from(args):
        if t0 is None:
            t0 = t
        if stop:
            break
        if args.mirror:
            img = cv2.flip(img, 1)
        rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
        ms = max(int((t - t0 if t0 is not None else 0) * 1000), last_ms + 1)  # VIDEO mode needs rising timestamps
        last_ms = ms
        res = pose.detect_for_video(image, ms)
        world = vis = None
        img_pts = None
        if res.pose_world_landmarks:
            world = [(p.x, p.y, p.z) for p in res.pose_world_landmarks[0]]
            vis = [p.visibility if p.visibility is not None else 1.0 for p in res.pose_world_landmarks[0]]
            img_pts = [(p.x, p.y) for p in res.pose_landmarks[0]]
        left = right = None
        if hands is not None and img_pts is not None:
            hr = hands.detect_for_video(image, ms)
            for norm, w in zip(hr.hand_landmarks, hr.hand_world_landmarks):
                wx, wy = norm[0].x, norm[0].y
                dl = (wx - img_pts[15][0]) ** 2 + (wy - img_pts[15][1]) ** 2
                dr = (wx - img_pts[16][0]) ** 2 + (wy - img_pts[16][1]) ** 2
                pts = [(p.x, p.y, p.z) for p in w]
                if dl < dr and left is None:
                    left = pts
                elif right is None:
                    right = pts
        data = packet(frame_no, t, world, vis, left, right, args.mirror)
        sock.sendto(data, ("127.0.0.1", args.port))
        if dump:
            dump.write(struct.pack("<I", len(data)) + data)
        frame_no += 1
        if args.preview:
            if img_pts is not None:
                h, w = img.shape[:2]
                for a, b in POSE_EDGES:
                    cv2.line(img, (int(img_pts[a][0] * w), int(img_pts[a][1] * h)),
                             (int(img_pts[b][0] * w), int(img_pts[b][1] * h)), (80, 220, 255), 2)
            now = time.monotonic()
            cv2.putText(img, "%.0f fps  %s" % (1.0 / max(now - shown, 1e-3), "tracking" if world else "no body"),
                        (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
            shown = now
            if args.preview_scale != 1.0:
                img = cv2.resize(img, None, fx=args.preview_scale, fy=args.preview_scale,
                                 interpolation=cv2.INTER_AREA)
            cv2.imshow("njin mocap (Esc to stop)", img)
            key = cv2.waitKey(1) & 0xFF
            if key == 27 or cv2.getWindowProperty("njin mocap (Esc to stop)", cv2.WND_PROP_VISIBLE) < 1:
                break
    if dump:
        dump.close()
    sock.close()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
