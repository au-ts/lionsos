# pdf_to_rgba_stream_hq.py
import io
import sys
from pathlib import Path

import fitz  # PyMuPDF
from PIL import Image, ImageFilter

TARGET_W, TARGET_H = 1920, 1080

def render_pdf_to_rgba_stream_hq(pdf_path: str, out_path: str,
                                 oversample: float = 8.0):
    pdf_path = Path(pdf_path)
    out_path = Path(out_path)

    # anti-alias to not make ur text look fuzzy
    fitz.TOOLS.set_aa_level(8)

    with fitz.open(pdf_path) as doc, open(out_path, "wb") as out:
        for page in doc:
            # Compute a scale that preserves aspect while making the *fitted* content
            # significantly larger than the final 1080p frame, then we'll downsample.
            rect = page.rect
            longer_pts = max(rect.width, rect.height)
            longer_px_target = max(TARGET_W, TARGET_H) * oversample
            scale = longer_px_target / longer_pts
            mat = fitz.Matrix(scale, scale)

            # Convert and fit to 1080p, stretch if the original image is not 16:9
            pm = page.get_pixmap(matrix=mat, colorspace=fitz.csRGB, alpha=False)
            img = Image.open(io.BytesIO(pm.tobytes("png"))).convert("RGBA")
            frame = img.resize((TARGET_W, TARGET_H), resample=Image.LANCZOS)

            out.write(frame.tobytes("raw", "RGBA"))

        num_pages = len(doc)

    frame_size = TARGET_W * TARGET_H * 4
    return num_pages, frame_size


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python pdf_to_rgba_stream_hq.py input.pdf output.rgba [oversample]")
        sys.exit(1)

    pdf, out = sys.argv[1], sys.argv[2]
    n, frame_size = render_pdf_to_rgba_stream_hq(pdf, out)
    print(f"Wrote {n} frames. Each frame = {frame_size} bytes (RGBA 1920x1080).")
