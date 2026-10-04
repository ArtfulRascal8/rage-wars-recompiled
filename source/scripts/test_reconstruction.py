import tempfile
import unittest
from pathlib import Path
from export_pc_source_candidate import audited_inputs
from reconstruction_common import apply_edit, digest, make_edit, require, safe_path, save, verify_source_candidate

class ReconstructionContracts(unittest.TestCase):
    def test_exact_patch_and_changed_preimage_rejection(self):
        old="void f() {\n    original();\n}\n"
        new="void f() {\n    integration();\n    original();\n}\n"
        edit=make_edit(old,new)
        self.assertEqual(apply_edit(old,edit),new)
        with self.assertRaises(ValueError):
            apply_edit(old.replace("original","older_behavior"),edit)

    def test_changed_patch_result_rejected(self):
        edit=make_edit("before\n","after\n")
        edit["changes"][0]["insert"]=["wrong\n"]
        with self.assertRaises(ValueError):
            apply_edit("before\n",edit)

    def test_output_escape_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                safe_path(Path(folder),"../private-output")

    def test_source_extra_file_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/"source.cpp").write_bytes(b"reviewed source\n")
            save(root/"source-manifest.json",{"schema":"xr64.private-reconstruction-source.v1",
                 "files":{"source.cpp":{"sha256":digest(b"reviewed source\n")}}})
            verify_source_candidate(root)
            (root/"rom.z64").write_bytes(b"private input")
            with self.assertRaises(ValueError):
                verify_source_candidate(root)

    def test_export_requires_real_audited_build(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                audited_inputs(Path(folder)/"nonexistent-build")
            with self.assertRaises(ValueError):
                audited_inputs(Path(folder))

    def test_required_gate_active(self):
        with self.assertRaises(ValueError):
            require(False,"identity mismatch")

if __name__=="__main__":
    unittest.main()
