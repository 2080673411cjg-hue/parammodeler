"""CPU loss/selection checks without importing the CUDA model implementation."""

import ast
from pathlib import Path
import unittest

import numpy as np
import torch
from torch import nn


source = Path(__file__).resolve().parents[1] / "scripts" / "main_reg.py"
tree = ast.parse(source.read_text(encoding="utf-8-sig"))
names = {"encode_targets", "decode_targets", "weighted_regression_loss",
         "symmetry_weighted_regression_loss", "symmetry_standardized_mae_score",
         "asym_ridge_180_index", "roof_ridge_180_index", "indented_180_permutation"}
nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
scope = {"np": np, "torch": torch,
         "INDENTED_180_MARGIN_PAIRS": (("geom_long_start_margin_ratio", "geom_long_end_margin_ratio"),
                                        ("geom_short_start_margin_ratio", "geom_short_end_margin_ratio"))}
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), "exec"), scope)


class RidgeSymmetryTests(unittest.TestCase):
    def test_physical_complement_with_noncentral_mean_and_std(self):
        true = torch.tensor([[10., .2], [12., .7]])
        pred = torch.tensor([[10., .8], [12., .7]], requires_grad=True)
        mean, std = torch.tensor([[9., .4]]), torch.tensor([[3., .2]])
        loss = scope["symmetry_weighted_regression_loss"](
            (pred-mean)/std, true, mean, std, torch.tensor([1., .9]),
            nn.SmoothL1Loss(reduction="none"), torch.arange(2), complement_index=1)
        self.assertAlmostEqual(loss.item(), 0., places=6)
        loss.backward()
        self.assertTrue(torch.isfinite(pred.grad).all())
        metrics = {"pred": pred.detach().numpy(), "true": true.numpy()}
        score = scope["symmetry_standardized_mae_score"](metrics, [3., .2], [1., .9], [0, 1], 1)
        self.assertAlmostEqual(score, 0., places=6)

    def test_center_prediction_is_penalized_and_gradient_moves_it(self):
        pred = torch.tensor([[11., .5]], requires_grad=True)
        true = torch.tensor([[10., .2]])
        loss = scope["symmetry_weighted_regression_loss"](
            pred, true, torch.zeros(1, 2), torch.ones(1, 2), torch.ones(2),
            nn.SmoothL1Loss(reduction="none"), torch.arange(2), complement_index=1)
        loss.backward()
        self.assertGreater(loss.item(), 0.)
        self.assertNotEqual(pred.grad[0, 1].item(), 0.)
        self.assertNotEqual(pred.grad[0, 0].item(), 0.)

    def test_other_classes_and_missing_target_are_rejected(self):
        check = scope["asym_ridge_180_index"]
        self.assertEqual(check("AsymmetricGableHouse", ["length", "ridgeRatio"]), 1)
        for name, targets in (("TwoGableHouses", ["ridgeRatio"]),
                              ("AsymmetricGableHouse", ["length"]),
                              ("AsymmetricGableHouse", ["ridgeRatio", "ridgeRatio"])):
            with self.assertRaises(ValueError):
                check(name, targets)

    def test_existing_indented_permutation_still_selects_whole_vector(self):
        true = np.array([[.1, .4, .2, .5]])
        metrics = {"true": true, "pred": true[:, [1, 0, 3, 2]]}
        score = scope["symmetry_standardized_mae_score"](metrics, np.ones(4), np.ones(4), [1, 0, 3, 2])
        self.assertEqual(score, 0.)

    def test_gabled_ridge_uses_same_valid_symmetry(self):
        self.assertEqual(scope["roof_ridge_180_index"]("GabledRoof", ["length", "ridgeRatio"]), 1)
        with self.assertRaises(ValueError):
            scope["roof_ridge_180_index"]("TwoGableHouses", ["ridgeRatio"])


if __name__ == "__main__":
    unittest.main()
