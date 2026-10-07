from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd


def print_table(title: str, table: pd.DataFrame) -> None:
    print(f"\n{title}")
    print(table.to_string(index=False))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    arguments = parser.parse_args()

    bones = pd.read_csv(arguments.directory / "bones.csv")
    points = pd.read_csv(arguments.directory / "points.csv")
    relationships = pd.read_csv(arguments.directory / "relationships.csv")
    jacobian = pd.read_csv(arguments.directory / "jacobian.csv")

    bone_summary = bones.groupby("bone").agg(
        correction_angle_mean=("correctionAngleDegrees", "mean"),
        correction_angle_max=("correctionAngleDegrees", "max"),
        correction_change_mean=("correctionChangeDegrees", "mean"),
        correction_change_max=("correctionChangeDegrees", "max"),
        initial_change_mean=("initialFrameChangeDegrees", "mean"),
        initial_change_max=("initialFrameChangeDegrees", "max"),
        optimized_change_mean=("optimizedFrameChangeDegrees", "mean"),
        optimized_change_max=("optimizedFrameChangeDegrees", "max"),
    ).reset_index()
    print_table("BONE SUMMARY", bone_summary.round(4))

    bone_frame = bones.groupby("frame").agg(
        correction_change_max=("correctionChangeDegrees", "max"),
        correction_change_rms=("correctionChangeDegrees", lambda values: np.sqrt(np.mean(values**2))),
        initial_change_max=("initialFrameChangeDegrees", "max"),
        optimized_change_max=("optimizedFrameChangeDegrees", "max"),
    ).reset_index()
    print_table(
        "TOP CORRECTION-CHANGE FRAMES",
        bone_frame.nlargest(15, "correction_change_max").round(4),
    )

    for prefix in ("initial", "optimized"):
        point_summary = points.groupby("point").agg(
            velocity_mean=(f"{prefix}Velocity", "mean"),
            velocity_max=(f"{prefix}Velocity", "max"),
            acceleration_mean=(f"{prefix}Acceleration", "mean"),
            acceleration_max=(f"{prefix}Acceleration", "max"),
            jerk_mean=(f"{prefix}Jerk", "mean"),
            jerk_max=(f"{prefix}Jerk", "max"),
        ).reset_index()
        print_table(f"{prefix.upper()} POINT MOTION", point_summary.round(5))

    correction_rows = []
    for point_name, group in points.groupby("point"):
        group = group.sort_values("frame")
        initial = group[["initialX", "initialY", "initialZ"]].to_numpy()
        optimized = group[["optimizedX", "optimizedY", "optimizedZ"]].to_numpy()
        correction = optimized - initial
        velocity = np.linalg.norm(np.diff(correction, axis=0), axis=1)
        acceleration = np.linalg.norm(np.diff(correction, n=2, axis=0), axis=1)
        jerk = np.linalg.norm(np.diff(correction, n=3, axis=0), axis=1)
        correction_rows.append({
            "point": point_name,
            "correction_velocity_mean": velocity.mean() if len(velocity) else 0.0,
            "correction_velocity_max": velocity.max() if len(velocity) else 0.0,
            "correction_acceleration_mean": acceleration.mean() if len(acceleration) else 0.0,
            "correction_acceleration_max": acceleration.max() if len(acceleration) else 0.0,
            "correction_jerk_mean": jerk.mean() if len(jerk) else 0.0,
            "correction_jerk_max": jerk.max() if len(jerk) else 0.0,
        })
    print_table(
        "OPTIMIZATION POINT-CORRECTION MOTION",
        pd.DataFrame(correction_rows).round(5),
    )

    relationship_summary = relationships.groupby(
        ["index", "firstPoint", "secondPoint"]
    ).agg(
        adaptive_mean=("adaptiveWeight", "mean"),
        adaptive_min=("adaptiveWeight", "min"),
        adaptive_max=("adaptiveWeight", "max"),
        optimized_distance_error_mean=("optimizedDistanceError", lambda values: np.mean(np.abs(values))),
        optimized_direction_error_mean=("optimizedDirectionError", "mean"),
    ).reset_index()
    print_table("RELATIONSHIP SUMMARY", relationship_summary.round(5))

    activation_rows = []
    for keys, group in relationships.groupby(["index", "firstPoint", "secondPoint"]):
        active = group[group["adaptiveWeight"] > 0.0]
        activation_rows.append({
            "index": keys[0],
            "relationship": f"{keys[1]} -> {keys[2]}",
            "first_active_frame": int(active["frame"].min()) if not active.empty else None,
            "last_active_frame": int(active["frame"].max()) if not active.empty else None,
        })
    print_table("RELATIONSHIP ACTIVATION", pd.DataFrame(activation_rows))

    singular_rows = []
    column_rows = []
    matrices = {}
    degree_labels = {}
    for frame, frame_rows in jacobian.groupby("frame"):
        points_order = list(dict.fromkeys(frame_rows["point"]))
        degrees_order = sorted(frame_rows["degree"].unique())
        matrix = np.zeros((len(points_order) * 3, len(degrees_order)))
        point_indices = {name: index for index, name in enumerate(points_order)}
        degree_indices = {degree: index for index, degree in enumerate(degrees_order)}
        for row in frame_rows.itertuples():
            point_index = point_indices[row.point]
            degree_index = degree_indices[row.degree]
            matrix[point_index * 3:point_index * 3 + 3, degree_index] = [
                row.dx,
                row.dy,
                row.dz,
            ]
        singular_values = np.linalg.svd(matrix, compute_uv=False)
        matrices[int(frame)] = matrix
        for degree, degree_group in frame_rows.groupby("degree"):
            first = degree_group.iloc[0]
            degree_labels[int(degree)] = f"{first['bone']}.{first['axis']}"
        tolerance = singular_values[0] * 1.0e-5
        nonzero = singular_values[singular_values > tolerance]
        smallest = nonzero[-1] if len(nonzero) else 0.0
        condition = singular_values[0] / smallest if smallest > 0.0 else np.inf
        singular_rows.append({
            "frame": frame,
            "rank": len(nonzero),
            "largest_singular": singular_values[0],
            "smallest_nonzero_singular": smallest,
            "condition": condition,
            "absolute_smallest_singular": singular_values[-1],
        })
        for degree, degree_group in frame_rows.groupby("degree"):
            first = degree_group.iloc[0]
            column_rows.append({
                "frame": frame,
                "degree": degree,
                "bone": first["bone"],
                "axis": first["axis"],
                "column_norm": np.sqrt(np.sum(
                    degree_group[["dx", "dy", "dz"]].to_numpy() ** 2
                )),
            })

    singular = pd.DataFrame(singular_rows)
    columns = pd.DataFrame(column_rows)
    print_table("JACOBIAN SUMMARY", singular.describe().round(5).reset_index())
    print_table("WORST-CONDITIONED FRAMES", singular.nlargest(15, "condition").round(5))
    print_table(
        "LOWEST DOF POINT SENSITIVITY",
        columns.nsmallest(20, "column_norm").round(5),
    )

    print("\nSMALLEST JACOBIAN DIRECTIONS")
    for frame in (0, 10, 20, 30, 37, 47, 60, 70):
        matrix = matrices[frame]
        _, singular_values, right_vectors = np.linalg.svd(matrix, full_matrices=False)
        print(f"frame {frame}: smallest singular values {singular_values[-3:]}")
        for vector_index in (-1, -2, -3, -4):
            vector = right_vectors[vector_index]
            terms = sorted(
                ((abs(value), degree_labels[index], value) for index, value in enumerate(vector)),
                reverse=True,
            )[:5]
            print(
                f"  s={singular_values[vector_index]:.9g}: "
                + ", ".join(f"{label}={value:+.4f}" for _, label, value in terms)
            )

    if {"parameterDeltaX", "parameterDeltaY", "parameterDeltaZ"}.issubset(bones.columns):
        projection_rows = []
        for frame, matrix in matrices.items():
            if frame == bones["frame"].min():
                continue
            frame_bones = bones[bones["frame"] == frame].set_index("bone")
            change = np.zeros(matrix.shape[1])
            for degree, label in degree_labels.items():
                bone, axis = label.rsplit(".", 1)
                change[degree] = frame_bones.loc[bone, f"parameterDelta{axis}"]
            _, singular_values, right_vectors = np.linalg.svd(matrix, full_matrices=False)
            coefficients = right_vectors @ change
            total_energy = float(change @ change)
            weak_two_energy = float(np.sum(coefficients[-2:] ** 2))
            weak_four_energy = float(np.sum(coefficients[-4:] ** 2))
            weak_six_energy = float(np.sum(coefficients[-6:] ** 2))
            weakest_energy = float(coefficients[-1] ** 2)
            projection_rows.append({
                "frame": frame,
                "change_degrees": np.degrees(np.sqrt(total_energy)),
                "weakest_projection_degrees": np.degrees(abs(coefficients[-1])),
                "weak_two_projection_degrees": np.degrees(np.sqrt(weak_two_energy)),
                "weakest_energy_fraction": weakest_energy / total_energy if total_energy else 0.0,
                "weak_two_energy_fraction": weak_two_energy / total_energy if total_energy else 0.0,
                "weak_four_energy_fraction": weak_four_energy / total_energy if total_energy else 0.0,
                "weak_six_energy_fraction": weak_six_energy / total_energy if total_energy else 0.0,
                "smallest_singular": singular_values[-1],
                "second_smallest_singular": singular_values[-2],
            })
        projections = pd.DataFrame(projection_rows)
        print_table(
            "TOP ROTATION CHANGES PROJECTED ONTO WEAK JACOBIAN MODES",
            projections.nlargest(20, "change_degrees").round(5),
        )
        print_table(
            "WEAK-MODE PROJECTION SUMMARY",
            projections.describe().round(5).reset_index(),
        )

    print("\nPER-ARM JACOBIAN CONDITION")
    for label, degrees in (("right", range(0, 6)), ("left", range(6, 12))):
        rows = []
        for frame, matrix in matrices.items():
            singular_values = np.linalg.svd(matrix[:, list(degrees)], compute_uv=False)
            rows.append({
                "frame": frame,
                "smallest": singular_values[-1],
                "condition": singular_values[0] / singular_values[-1],
            })
        table = pd.DataFrame(rows)
        print(f"{label}: smallest range {table['smallest'].min():.9g}..{table['smallest'].max():.9g}; condition range {table['condition'].min():.3f}..{table['condition'].max():.3f}")

    merged = bone_frame.merge(singular, on="frame")
    print("\nCORRELATIONS")
    print(
        merged[[
            "correction_change_max",
            "condition",
            "smallest_nonzero_singular",
        ]].corr().round(5).to_string()
    )


if __name__ == "__main__":
    main()
