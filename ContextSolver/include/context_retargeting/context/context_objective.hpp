#pragma once

#include "context_retargeting/context/context_definition.hpp"
#include "context_retargeting/optimization/numerical_gradient.hpp"
#include "context_retargeting/optimization/types.hpp"

#include <cstddef>
#include <vector>

namespace context_retargeting::context {

struct ContextGradientVectors
{
    optimization::ParameterVector distance;
    optimization::ParameterVector direction;
    optimization::ParameterVector penetration;
    optimization::ParameterVector height;
    optimization::ParameterVector pointPositionRegularization;
    optimization::ParameterVector pointJerk;
    optimization::ParameterVector total;
};

class ContextObjective
{
public:
    ContextObjective(
        const ContextDefinition& definition,
        const ContextBatchInput& input,
        ContextLossWeights lossWeights);

    [[nodiscard]] std::size_t parameterCount() const noexcept;

    [[nodiscard]] ContextLossComponents evaluateLossComponents(
        optimization::ConstParameterView parameters);

    [[nodiscard]] Scalar evaluateLoss(
        optimization::ConstParameterView parameters);

    void setTargetAdaptiveWeightMix(Scalar targetAdaptiveWeightMix);

    [[nodiscard]] ContextGradientMagnitudes evaluateCentralDifferenceGradient(
        optimization::ConstParameterView parameters,
        optimization::ParameterView gradientOut,
        const optimization::CentralDifferenceSettings& settings,
        ContextGradientVectors* gradientVectorsOut = nullptr);

    [[nodiscard]] PoseBatch createTargetPoses(
        optimization::ConstParameterView parameters) const;

private:
    using PointPositions = std::vector<Vector3>;
    using PointNormals = std::vector<Vector3>;

    void applyParameters(
        optimization::ConstParameterView parameters,
        PoseBatch& targetPoses) const;

    void evaluatePointGeometry(
        const CharacterDefinition& character,
        const PoseBatch& poses,
        std::vector<GlobalPose>& globalPoses,
        std::vector<PointPositions>& pointPositions,
        std::vector<PointNormals>& pointNormals) const;

    [[nodiscard]] ContextLossComponents evaluateSpatialLossComponents(
        std::size_t frame,
        const PointPositions& candidatePoints,
        const PointNormals& candidateNormals,
        const std::vector<Scalar>* frozenTargetAdaptiveWeights = nullptr) const;

    [[nodiscard]] std::vector<Scalar> calculateTargetAdaptiveWeights(
        const PointPositions& candidatePoints) const;

    [[nodiscard]] Scalar evaluateJerkLoss(
        std::size_t firstFrame,
        std::size_t replacementFrame,
        const PointPositions* replacementPositions) const;

    [[nodiscard]] ContextLossComponents evaluateAffectedLossComponents(
        std::size_t frame,
        const PointPositions& candidatePoints,
        const PointNormals& candidateNormals,
        const std::vector<Scalar>& frozenTargetAdaptiveWeights) const;

    void evaluatePerturbedFrameGeometry(
        optimization::ConstParameterView parameters,
        std::size_t frame,
        std::size_t degreeToOffset,
        Scalar parameterOffset,
        PointPositions& pointPositions,
        PointNormals& pointNormals) const;

    const ContextDefinition& definition_;
    const ContextBatchInput& input_;
    ContextLossWeights lossWeights_;
    Scalar targetAdaptiveWeightMix_ = 0.0;

    std::vector<GlobalPose> sourceGlobalPoses_;
    std::vector<PointPositions> sourcePointPositions_;
    std::vector<PointNormals> sourcePointNormals_;
    std::vector<GlobalPose> initialTargetGlobalPoses_;
    std::vector<PointPositions> initialTargetPointPositions_;
    std::vector<PointNormals> initialTargetPointNormals_;

    PoseBatch candidateTargetPoses_;
    std::vector<GlobalPose> candidateTargetGlobalPoses_;
    std::vector<PointPositions> candidateTargetPointPositions_;
    std::vector<PointNormals> candidateTargetPointNormals_;
};

} // namespace context_retargeting::context
