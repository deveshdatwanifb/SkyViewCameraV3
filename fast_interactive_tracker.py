import cv2
import numpy as np
from scipy.optimize import minimize
import os
import time

# --- Camera Intrinsics ---
K = np.array([[1452.77568,    0.00000, 660.577237],
              [   0.00000, 1093.45734, 402.658170],
              [   0.00000,    0.00000,   1.000000]], dtype=np.float32)

dist_coeffs = np.array([[-3.44945310e-01, 1.20150878e+00, -8.67942063e-04, -1.22818271e-03, -4.06702698e+00]], dtype=np.float32)

# Global UI States
trackbar_moved = False
current_frame_idx = 0
pose_7d = np.array([0.0, -1.57, 0.0, 0.0, 0.0, 40.0, 1.0], dtype=np.float64)

# Interactive Mouse State
active_axis = None  
last_mouse_y = 0

GIZMO_CENTER = (150, 120)
GIZMO_RADIUS = 35
GIZMO_SPACING = 90

def on_trackbar(val):
    global trackbar_moved, current_frame_idx
    current_frame_idx = val
    trackbar_moved = True

def mouse_callback(event, x, y, flags, param):
    global pose_7d, active_axis, last_mouse_y
    centers = [
        (GIZMO_CENTER[0], GIZMO_CENTER[1]),
        (GIZMO_CENTER[0] + GIZMO_SPACING, GIZMO_CENTER[1]),
        (GIZMO_CENTER[0] + 2 * GIZMO_SPACING, GIZMO_CENTER[1])
    ]
    if event == cv2.EVENT_LBUTTONDOWN:
        for idx, center in enumerate(centers):
            dist = np.sqrt((x - center[0])**2 + (y - center[1])**2)
            if dist <= GIZMO_RADIUS:
                active_axis = idx
                last_mouse_y = y
                break
    elif event == cv2.EVENT_MOUSEMOVE and (flags & cv2.EVENT_FLAG_LBUTTON):
        if active_axis is not None:
            delta_y = y - last_mouse_y
            pose_7d[active_axis] += delta_y * 0.01
            last_mouse_y = y
    elif event == cv2.EVENT_LBUTTONUP:
        active_axis = None

def load_full_mesh_obj(filepath):
    if not os.path.exists(filepath): return None, None
    vertices, faces = [], []
    with open(filepath, 'r') as f:
        for line in f:
            if line.startswith('v '):
                parts = line.strip().split()
                vertices.append([float(parts[1]), float(parts[2]), float(parts[3])])
            elif line.startswith('f '):
                parts = line.strip().split()[1:]
                faces.append([int(p.split('/')[0]) - 1 for p in parts])
    return np.ascontiguousarray(vertices, dtype=np.float32), faces

def extract_dense_foreground(frame):
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(gray, (5, 5), 0)
    _, thresh = cv2.threshold(blurred, 180, 255, cv2.THRESH_BINARY)
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
    return cv2.morphologyEx(thresh, cv2.MORPH_CLOSE, kernel)

def render_fast_hull_silhouette(vertices, rvec, tvec, h, w, downsample_factor=4):
    """Bypasses slow pure-Python face loops using C++ vectorized Convex Hulls."""
    scaled_h, scaled_w = h // downsample_factor, w // downsample_factor
    cad_mask = np.zeros((scaled_h, scaled_w), dtype=np.uint8)
    
    K_scaled = K / downsample_factor
    K_scaled[2, 2] = 1.0
    
    points_2d, _ = cv2.projectPoints(vertices, rvec, tvec, K_scaled, distCoeffs=None)
    points_2d = np.int32(points_2d).reshape(-1, 2)
    
    # Fast C++ calculation of the shape perimeter
    hull = cv2.convexHull(points_2d)
    cv2.fillPoly(cad_mask, [hull], 255)
    return cad_mask

def dense_registration_cost(params, vertices, real_mask_downsampled):
    rvec, tvec, scale = params[:3], params[3:6], params[6]
    if scale < 0.001: return 1.0
    h, w = real_mask_downsampled.shape
    
    # Math evaluates instantly on downsampled hulls
    cad_mask = render_fast_hull_silhouette(vertices * scale, rvec, tvec, h * 4, w * 4, downsample_factor=4)
    
    intersection = cv2.bitwise_and(real_mask_downsampled, cad_mask)
    union = cv2.bitwise_or(real_mask_downsampled, cad_mask)
    n_inter = np.sum(intersection > 0)
    n_union = np.sum(union > 0)
    return 1.0 - (n_inter / n_union) if n_union > 0 else 1.0

def draw_gizmo_ui(overlay):
    colors = [(0, 0, 255), (0, 255, 0), (255, 0, 0)]
    labels = ["PITCH (Rx)", "YAW (Ry)", "ROLL (Rz)"]
    for idx in range(3):
        cx = GIZMO_CENTER[0] + idx * GIZMO_SPACING
        cy = GIZMO_CENTER[1]
        thickness = 3 if active_axis == idx else 1
        cv2.circle(overlay, (cx, cy), GIZMO_RADIUS, colors[idx], thickness, cv2.LINE_AA)
        cv2.circle(overlay, (cx, cy), 2, colors[idx], -1)
        cv2.putText(overlay, labels[idx], (cx - 30, cy + GIZMO_RADIUS + 15), 
                    cv2.FONT_HERSHEY_SIMPLEX, 0.35, colors[idx], 1, cv2.LINE_AA)

def main():
    global trackbar_moved, current_frame_idx, pose_7d

    obj_path = "5170707.obj"
    vertices, all_faces = load_full_mesh_obj(obj_path)
    if vertices is None: return
    
    # Highly downsample vertices used strictly by optimizer math
    vertices_optimized = vertices[::4]

    video_path = "/Users/devesh-fyveby/Downloads/wilson/cam1.mkv"
    cap = cv2.VideoCapture(video_path)

    window_name = "Interactive Highly-Optimized Registration Canvas"
    cv2.namedWindow(window_name)
    cv2.setMouseCallback(window_name, mouse_callback)
    cv2.createTrackbar("Timeline", window_name, 0, int(cap.get(cv2.CAP_PROP_FRAME_COUNT)) - 1, on_trackbar)

    is_paused = True
    real_dense_mask = None
    real_mask_downsampled = None
    cached_frame_idx = -1

    while True:
        t_loop_start = time.time()

        if trackbar_moved:
            cap.set(cv2.CAP_PROP_POS_FRAMES, current_frame_idx)
            trackbar_moved = False

        # 1. Profile Frame Reading
        t0 = time.time()
        if not is_paused:
            ret, frame = cap.read()
            if not ret: break
            current_frame_idx = int(cap.get(cv2.CAP_PROP_POS_FRAMES))
            cv2.setTrackbarPos("Timeline", window_name, current_frame_idx)
        else:
            cap.set(cv2.CAP_PROP_POS_FRAMES, current_frame_idx)
            ret, frame = cap.read()
            if not ret: break
        t_video_io = (time.time() - t0) * 1000

        # 2. Profile Mask Extraction and Downsampling
        t0 = time.time()
        undistorted_frame = cv2.undistort(frame, K, dist_coeffs)
        h, w, _ = undistorted_frame.shape

        if current_frame_idx != cached_frame_idx or real_dense_mask is None:
            real_dense_mask = extract_dense_foreground(undistorted_frame)
            real_mask_downsampled = cv2.resize(real_dense_mask, (w // 4, h // 4), interpolation=cv2.INTER_NEAREST)
            cached_frame_idx = current_frame_idx
        t_masking = (time.time() - t0) * 1000

        # 3. Profile Optimizer Loop
        t0 = time.time()
        if not is_paused:
            result = minimize(
                dense_registration_cost,
                pose_7d,
                args=(vertices_optimized, real_mask_downsampled),
                method='Nelder-Mead',
                options={'maxiter': 10} # Kept small for instant convergence
            )
            pose_7d = result.x
        t_optimizer = (time.time() - t0) * 1000

        # 4. Profile UI Rendering (Optimized: No slow polygon fills on main frame)
        t0 = time.time()
        rvec, tvec, scale = pose_7d[:3], pose_7d[3:6], pose_7d[6]
        
        points_2d, _ = cv2.projectPoints(vertices * scale, rvec, tvec, K, dist_coeffs)
        points_2d = np.int32(points_2d).reshape(-1, 2)

        overlay = undistorted_frame.copy()
        
        # Fast sparse rendering for high UI frame rates
        for f in all_faces[::12]: 
            if all(idx < len(points_2d) for idx in f):
                for i in range(len(f)):
                    cv2.line(overlay, tuple(points_2d[f[i]]), tuple(points_2d[f[(i+1)%len(f)]]), (0, 255, 255), 1, cv2.LINE_AA)

        cv2.drawFrameAxes(overlay, K, dist_coeffs, rvec, tvec, 5.0, 2)
        draw_gizmo_ui(overlay)

        if is_paused:
            cv2.putText(overlay, "MANUAL ALIGNMENT MODE: Drag rings to rotate, use keys to shift", (20, h - 50), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 1, cv2.LINE_AA)
            cv2.putText(overlay, "Press SPACE to unpause and run tracker", (20, h - 25), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1, cv2.LINE_AA)

        cv2.imshow(window_name, overlay)
        t_ui_draw = (time.time() - t0) * 1000

        # Keyboard Intercept Handles
        key = cv2.waitKey(1) & 0xFF
        if key == ord('q'): break
        elif key == ord(' '): is_paused = not is_paused

        if is_paused:
            step_t = 0.5
            if key == ord('a'): pose_7d[3] -= step_t
            elif key == ord('d'): pose_7d[3] += step_t
            elif key == ord('r'): pose_7d[4] -= step_t
            elif key == ord('f'): pose_7d[4] += step_t
            elif key == ord('w'): pose_7d[5] -= step_t
            elif key == ord('s'): pose_7d[5] += step_t
            elif key == ord('z'): pose_7d[6] *= 0.98
            elif key == ord('x'): pose_7d[6] *= 1.02

        # Telemetry calculations
        t_total_loop = (time.time() - t_loop_start) * 1000
        fps = 1000.0 / t_total_loop if t_total_loop > 0 else 0.0
        
        print(f"FPS: {fps:5.1f} | Loop: {t_total_loop:5.1f}ms | Video IO: {t_video_io:4.1f}ms | "
              f"Masking: {t_masking:4.1f}ms | Optimizer: {t_optimizer:5.1f}ms | UI Draw: {t_ui_draw:4.1f}ms", end="\r")

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
