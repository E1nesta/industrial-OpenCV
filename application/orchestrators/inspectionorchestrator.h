#pragma once

#include <QString>

#include "application/state/inspectionsessionstate.h"
#include "domain/entities/capturedframe.h"
#include "domain/entities/inputsource.h"
#include "domain/entities/inspectiontask.h"
#include "domain/entities/recipe.h"

class InspectionOrchestrator
{
public:
    bool startInspectionFromFile(
        const QString &imagePath,
        const Recipe &recipe,
        InspectionSessionState &sessionState,
        InspectionTask *task,
        QString *errorMessage) const;

    bool startInspectionFromFrame(
        const CapturedFrame &frame,
        const CaptureStatusSnapshot &captureStatus,
        const Recipe &recipe,
        InspectionSessionState &sessionState,
        InspectionTask *task,
        QString *errorMessage) const;

    bool canStartContinuousInspection(
        const CaptureStatusSnapshot &captureStatus,
        const CapturedFrame &latestFrame,
        const InspectionSessionState &sessionState,
        QString *errorMessage) const;

    bool shouldTriggerContinuousInspection(
        const CaptureStatusSnapshot &captureStatus,
        const CapturedFrame &latestFrame,
        const InspectionSessionState &sessionState) const;

    void markContinuousInspectionTriggered(
        const CapturedFrame &frame,
        InspectionSessionState &sessionState) const;

private:
    QString createInspectionId() const;

    bool buildInspectionTask(
        const CapturedFrame &frame,
        const QString &inspectionId,
        const Recipe &recipe,
        InspectionTask *task,
        QString *errorMessage) const;

    bool buildFileInspectionTask(
        const QString &imagePath,
        const QString &inspectionId,
        const Recipe &recipe,
        InspectionTask *task,
        QString *errorMessage) const;
};
