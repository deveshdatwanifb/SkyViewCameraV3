import os
import threading
import cv2
import time
import logging

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

RTSP_URL_1 = "rtsp://admin:123456@192.168.69.199/Channels/101"
RTSP_URL_2 = "rtsp://admin:123456@192.168.69.187/Channels/101"

WIDTH, HEIGHT = 640, 480


class RTSPStreamReader:

    def __init__(self, src):
        self.cap = cv2.VideoCapture(src)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        self.ret = False
        self.frame = None
        self.started = False
        self.read_lock = threading.Lock()

    def start(self):
        if self.started:
            return self
        self.started = True
        self.thread = threading.Thread(target=self.update, args=())
        self.thread.daemon = True
        self.thread.start()
        return self

    def update(self):
        while self.started:
            ret, frame = self.cap.read()
            if not ret:
                continue
            with self.read_lock:
                self.ret = ret
                self.frame = frame

    def read(self):
        with self.read_lock:
            if self.frame is not None:
                return self.ret, self.frame.copy()
            return self.ret, None

    def stop(self):
        self.started = False
        if self.thread.is_alive():
            self.thread.join()
        self.cap.release()


def main():
    airport_code = input("Enter airport code: ").strip().upper()
    dir_name = f"image_dump_{airport_code}"
    os.makedirs(dir_name, exist_ok=True)

    logging.info("Connecting to RTSP streams... Please wait.")
    stream1 = RTSPStreamReader(RTSP_URL_1).start()
    stream2 = RTSPStreamReader(RTSP_URL_2).start()

    paused = False
    last_combined_frame = None
    latest_raw_frame1 = None
    latest_raw_frame2 = None

    logging.info("Controls: [SPACE]/[P] Pause/Resume | [S] Save PNG | [Q] Quit")

    while True:
        if not paused:
            ret1, frame1 = stream1.read()
            ret2, frame2 = stream2.read()

            if ret1 and frame1 is not None and ret2 and frame2 is not None:
                latest_raw_frame1 = frame1.copy()
                latest_raw_frame2 = frame2.copy()
                f1_resized = cv2.resize(frame1, (WIDTH, HEIGHT))
                f2_resized = cv2.resize(frame2, (WIDTH, HEIGHT))
                combined = cv2.hconcat([f1_resized, f2_resized])
                cv2.putText(combined, "LIVE", (20, 40),
                            cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
                last_combined_frame = combined.copy()
            
            else:
                if last_combined_frame is None:
                    continue
                combined = last_combined_frame
        
        else:
            if last_combined_frame is not None:
                combined = last_combined_frame.copy()
                cv2.putText(combined, "PAUSED (SYNC CHECK)", (20, 40),
                            cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 2)

        cv2.imshow("RTSP Dual-Sync Tester", combined)
        key = cv2.waitKey(1) & 0xFF
        
        if key == ord('p') or key == 32:
            paused = not paused

            if paused:
                logging.info("Paused. Check sync now.")
            else:
                logging.info("Resumed live streams.")
        
        elif key == ord('s') or key == ord('S'):
        
            if latest_raw_frame1 is not None and latest_raw_frame2 is not None:
                timestamp = time.strftime("%Y%m%d_%H%M%S")
                filename1 = os.path.join(dir_name, f"{airport_code}_{timestamp}_stream1.png")
                filename2 = os.path.join(dir_name, f"{airport_code}_{timestamp}_stream2.png")        
                cv2.imwrite(filename1, latest_raw_frame1, [cv2.IMWRITE_PNG_COMPRESSION, 3])
                cv2.imwrite(filename2, latest_raw_frame2, [cv2.IMWRITE_PNG_COMPRESSION, 3])
                logging.info(f"Saved full-res lossless images to {dir_name}/")
        
            else:
                logging.warning("No frames available to save yet.")
        
        elif key == ord('q'):
            break

    logging.info("Closing streams...")
    stream1.stop()
    stream2.stop()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()