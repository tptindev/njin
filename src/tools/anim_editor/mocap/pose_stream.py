"""Webcam (or video) body tracking for the njin Animation Editor.

Runs MediaPipe Pose Landmarker (and optionally Hand Landmarker) on each frame
and sends the 3D world landmarks to the editor as one UDP packet per frame on
127.0.0.1. The editor starts this script itself (Mocap > Start); it can also be
run by hand, then the editor only listens.

  pose_stream.py [--camera 0] [--port 47800] [--hands] [--mirror] [--view] [--preview] [--preview-scale 0.5]
                 [--model pose_landmarker_heavy.task]
  pose_stream.py --video clip.mp4 [--dump out.bin]      # tests: a file instead
  pose_stream.py --frames folder [--fps 30] [--dump out.bin]

--view sends the camera image, with the tracked skeleton drawn on it, to the
editor's Camera window: JPEG, --view-width pixels wide (320), at most
--view-fps frames a second (15), on --view-port (the landmark port + 1).
--preview opens an OpenCV window instead (or as well), --preview-scale per side.

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

View packet (little-endian, version 1), one UDP datagram per frame, at most VIEW_MAX bytes:
  char[4]  magic "NJMV"
  u16      version (1)
  u16      flags: bit0 pose found
  u32      frame number (the landmark packet's)
  u16      width, u16 height of the JPEG
  u32      JPEG size in bytes, then the JPEG

Hands are tracked in a square cut around each pose wrist (one Hand Landmarker
per side), so that a hand stays large enough for every finger joint even when
the whole body is in view; they are given as the person's left/right by the
wrist they were cut around, not MediaPipe's handedness label (which assumes a
mirrored selfie image). --mirror flips the image first, so the character moves
like a mirror image.

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
import numpy as np
from mediapipe.tasks.python import BaseOptions, vision

HERE = os.path.dirname(os.path.abspath(__file__))
MAGIC = b"NJMC"
VERSION = 1
VIEW_MAGIC = b"NJMV"
VIEW_MAX = 60000  # bytes in one datagram, header included
POSE_EDGES = [(11, 12), (11, 13), (13, 15), (12, 14), (14, 16), (11, 23), (12, 24), (23, 24), (23, 25), (25, 27),
              (24, 26), (26, 28), (27, 29), (29, 31), (27, 31), (28, 30), (30, 32), (28, 32),
              (15, 17), (15, 19), (17, 19), (16, 18), (16, 20), (18, 20), (2, 7), (5, 8), (2, 5)]
HAND_EDGES = [(0, 1), (1, 2), (2, 3), (3, 4), (0, 5), (5, 6), (6, 7), (7, 8), (5, 9), (9, 10), (10, 11), (11, 12),
              (9, 13), (13, 14), (14, 15), (15, 16), (13, 17), (0, 17), (17, 18), (18, 19), (19, 20)]
# Pose points of each side: wrist, elbow, pinky and index knuckles.
HAND_POSE = ((15, 13, 17, 19), (16, 14, 18, 20))


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


def hand_box(img_pts, vis, side, w, h):
    """The square (x, y, size) in pixels around one hand, from the pose; None if the wrist is not seen."""
    wrist, elbow, pinky, index = HAND_POSE[side]
    if min(vis[wrist], vis[pinky], vis[index]) < 0.3:
        return None
    px = lambda i: (img_pts[i][0] * w, img_pts[i][1] * h)
    wx, wy = px(wrist)
    kx = (px(pinky)[0] + px(index)[0]) / 2 - wx
    ky = (px(pinky)[1] + px(index)[1]) / 2 - wy
    reach = (kx * kx + ky * ky) ** 0.5
    forearm = ((px(elbow)[0] - wx) ** 2 + (px(elbow)[1] - wy) ** 2) ** 0.5
    # The pose's knuckles are near the wrist; fingers reach about three times
    # as far. The forearm keeps the box large when the hand is seen end on.
    size = max(4.0 * reach, 0.9 * forearm, 48.0)
    cx, cy = (wx + kx / reach * 0.35 * size, wy + ky / reach * 0.35 * size) if reach > 1e-3 else (wx, wy)
    return int(cx - size / 2), int(cy - size / 2), int(size)


def crop(rgb, box):
    """The box cut from the image, black where it leaves the image."""
    x, y, s = box
    h, w = rgb.shape[:2]
    out = np.zeros((s, s, 3), dtype=rgb.dtype)
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + s, w), min(y + s, h)
    if x1 > x0 and y1 > y0:
        out[y0 - y:y1 - y, x0 - x:x1 - x] = rgb[y0:y1, x0:x1]
    return out


def view_packet(img, img_pts, hand_pts, frame_no, tracking, width):
    """The image scaled to `width` with the skeleton on it, as JPEG in one datagram; None if it cannot fit."""
    h, w = img.shape[:2]
    for scale in (1.0, 0.75, 0.5):
        vw = max(16, int(width * scale))
        vh = max(16, int(h * vw / w))
        small = cv2.resize(img, (vw, vh), interpolation=cv2.INTER_AREA)
        if img_pts is not None:
            draw_skeleton(small, img_pts, hand_pts)  # after scaling, so the lines stay 2 px wide
        for quality in (70, 55, 40):
            ok, jpeg = cv2.imencode(".jpg", small, [cv2.IMWRITE_JPEG_QUALITY, quality])
            if not ok:
                return None
            head = VIEW_MAGIC + struct.pack("<HHIHHI", VERSION, 1 if tracking else 0, frame_no, vw, vh, len(jpeg))
            if len(head) + len(jpeg) <= VIEW_MAX:
                return head + jpeg.tobytes()
    return None


def draw_skeleton(img, img_pts, hand_pts=()):
    h, w = img.shape[:2]
    thick = max(2, w // 400)
    for a, b in POSE_EDGES:
        cv2.line(img, (int(img_pts[a][0] * w), int(img_pts[a][1] * h)),
                 (int(img_pts[b][0] * w), int(img_pts[b][1] * h)), (80, 220, 255), thick)
    for pts in hand_pts:
        for a, b in HAND_EDGES:
            cv2.line(img, (int(pts[a][0] * w), int(pts[a][1] * h)), (int(pts[b][0] * w), int(pts[b][1] * h)),
                     (120, 255, 120), max(1, thick // 2))


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
    ap.add_argument("--view", action="store_true", help="send the camera image to the editor's Camera window")
    ap.add_argument("--view-port", type=int, help="port for --view (default: --port + 1)")
    ap.add_argument("--view-width", type=int, default=320)
    ap.add_argument("--view-fps", type=float, default=15.0)
    ap.add_argument("--preview", action="store_true", help="also show an OpenCV window")
    ap.add_argument("--preview-scale", type=float, default=0.5,
                    help="size of the preview window per side (0.5 = a quarter of the image area)")
    ap.add_argument("--dump", help="also append every packet to this file (tests)")
    ap.add_argument("--model", default=os.path.join(HERE, "pose_landmarker_full.task"),
                    help="pose_landmarker_heavy.task is more exact, and slower")
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
        # One per side: each sees only the cut around its own wrist.
        hands = [vision.HandLandmarker.create_from_options(vision.HandLandmarkerOptions(
            base_options=BaseOptions(model_asset_path=hand_model), running_mode=vision.RunningMode.VIDEO,
            num_hands=1, min_hand_detection_confidence=0.4, min_hand_presence_confidence=0.4,
            min_tracking_confidence=0.4)) for _ in range(2)]

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dump = open(args.dump, "wb") if args.dump else None
    last_ms = -1
    frame_no = 0
    view_port = args.view_port if args.view_port else args.port + 1
    view_at = 0.0
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
        found = [None, None]
        hand_pts = []
        if hands is not None and img_pts is not None:
            h, w = rgb.shape[:2]
            for side in (0, 1):
                box = hand_box(img_pts, vis, side, w, h)
                if box is None:
                    continue
                cut = mp.Image(image_format=mp.ImageFormat.SRGB, data=crop(rgb, box))
                hr = hands[side].detect_for_video(cut, ms)
                if hr.hand_world_landmarks:
                    # World landmarks do not change with the cut: it is only moved and scaled.
                    found[side] = [(p.x, p.y, p.z) for p in hr.hand_world_landmarks[0]]
                    x, y, s = box
                    hand_pts.append([((x + p.x * s) / w, (y + p.y * s) / h) for p in hr.hand_landmarks[0]])
        left, right = found
        data = packet(frame_no, t, world, vis, left, right, args.mirror)
        sock.sendto(data, ("127.0.0.1", args.port))
        if dump:
            dump.write(struct.pack("<I", len(data)) + data)
        if args.view and time.monotonic() - view_at >= 1.0 / max(args.view_fps, 1.0):
            view_at = time.monotonic()
            data = view_packet(img, img_pts, hand_pts, frame_no, world is not None, args.view_width)
            if data:
                sock.sendto(data, ("127.0.0.1", view_port))
        frame_no += 1
        if args.preview:
            if img_pts is not None:
                draw_skeleton(img, img_pts, hand_pts)
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
