import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from feedback_oracle import WIDTH,HEIGHT,spatial_frames,compare_png_pixels

class SpatialOracleTests(unittest.TestCase):
    def test_orientation_and_source_negative_controls(self):
        frames=dict(spatial_frames((1,2)))
        first=frames[1]
        # PNG order deliberately differs from the oracle's GL origin.
        stride=WIDTH*4
        png=b''.join(first[y*stride:(y+1)*stride] for y in reversed(range(HEIGHT)))
        self.assertEqual(compare_png_pixels(png,first)['bad_pixels'],0)
        self.assertGreater(compare_png_pixels(first,first)['bad_pixels'],WIDTH*HEIGHT*.01)
        unshifted=dict(spatial_frames((1,2),source_dx=0,source_dy=0))
        self.assertEqual(first,unshifted[1])
        bad=sum(max(abs(a-b) for a,b in zip(frames[2][i:i+4],unshifted[2][i:i+4]))>2 for i in range(0,len(first),4))
        self.assertGreater(bad,WIDTH*HEIGHT*.01)

    def test_flip_x_transpose_and_shifted_negative_controls(self):
        frames=dict(spatial_frames((1,2)))
        frame=frames[1]
        wrong_x=bytearray(len(frame)); transposed=bytearray(len(frame))
        for y in range(HEIGHT):
            for x in range(WIDTH):
                i=(y*WIDTH+x)*4
                j=(y*WIDTH+WIDTH-1-x)*4
                wrong_x[i:i+4]=frame[j:j+4]
                # Treat the same payload as a 600x800 image and transpose it.
                j=(x*HEIGHT+y)*4
                transposed[i:i+4]=frame[j:j+4]
        for negative in [wrong_x,transposed]:
            bad=sum(max(abs(a-b) for a,b in zip(frame[i:i+4],negative[i:i+4]))>2 for i in range(0,len(frame),4))
            self.assertGreater(bad,WIDTH*HEIGHT*.01)
        shifted=dict(spatial_frames((1,2),source_dx=3,source_dy=2))
        self.assertEqual(frames[1],shifted[1])
        bad=sum(max(abs(a-b) for a,b in zip(frames[2][i:i+4],shifted[2][i:i+4]))>2 for i in range(0,len(frame),4))
        self.assertGreater(bad,WIDTH*HEIGHT*.01)
