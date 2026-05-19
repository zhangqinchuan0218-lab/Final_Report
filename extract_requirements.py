from pathlib import Path
from docx import Document

pdf = Path(r"C:\Users\61461\Downloads\GENG4412_GENG5512 Final Report Rubric S1 2026 - Tagged.pdf")
template = Path(r"C:\Users\61461\Downloads\GENG4412_GENG5512 Final Report Template S1 2026.docx")

doc = Document(template)
print("TEMPLATE OUTLINE")
for i, p in enumerate(doc.paragraphs, 1):
    text = " ".join(p.text.split())
    if not text:
        continue
    if p.style.name.startswith("Heading") or text[:2].isdigit() or text.isupper() or text in {
        "Project Summary", "Acknowledgements", "Nomenclature", "References", "Appendices"
    }:
        print(f"{i:03d}: [{p.style.name}] {text[:180]}")

print("\nPDF TEXT")
try:
    from pypdf import PdfReader
except Exception:
    from PyPDF2 import PdfReader
reader = PdfReader(str(pdf))
for idx, page in enumerate(reader.pages, 1):
    text = page.extract_text() or ""
    print(f"\n--- PAGE {idx} ---")
    print(text[:3500])
