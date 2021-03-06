/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include <projectpart.h>
#include <projectpartsdonotexistexception.h>
#include <projects.h>
#include <translationunitdoesnotexistexception.h>
#include <translationunitalreadyexistsexception.h>
#include <translationunitfilenotexitexception.h>
#include <clangtranslationunit.h>
#include <translationunitisnullexception.h>
#include <translationunits.h>
#include <unsavedfiles.h>
#include <utf8string.h>

#include <clang-c/Index.h>

#include <gmock/gmock.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include "gtest-qt-printing.h"

using ClangBackEnd::TranslationUnit;
using ClangBackEnd::UnsavedFiles;
using ClangBackEnd::ProjectPart;
using ClangBackEnd::ProjectPartContainer;

using testing::IsNull;
using testing::NotNull;
using testing::Gt;
using testing::Not;
using testing::Contains;

namespace {

using ::testing::PrintToString;

MATCHER_P3(IsTranslationUnit, filePath, projectPartId, documentRevision,
           std::string(negation ? "isn't" : "is")
           + " translation unit with file path "+ PrintToString(filePath)
           + " and project " + PrintToString(projectPartId)
           + " and document revision " + PrintToString(documentRevision)
           )
{
    return arg.filePath() == filePath
        && arg.projectPartId() == projectPartId
        && arg.documentRevision() == documentRevision;
}

class TranslationUnits : public ::testing::Test
{
protected:
    void SetUp() override;

protected:
    ClangBackEnd::ProjectParts projects;
    ClangBackEnd::UnsavedFiles unsavedFiles;
    ClangBackEnd::TranslationUnits translationUnits{projects, unsavedFiles};
    const Utf8String filePath = Utf8StringLiteral(TESTDATA_DIR"/translationunits.cpp");
    const Utf8String headerPath = Utf8StringLiteral(TESTDATA_DIR"/translationunits.h");
    const Utf8String nonExistingFilePath = Utf8StringLiteral("foo.cpp");
    const Utf8String projectPartId = Utf8StringLiteral("projectPartId");
    const Utf8String otherProjectPartId = Utf8StringLiteral("otherProjectPartId");
    const Utf8String nonExistingProjectPartId = Utf8StringLiteral("nonExistingProjectPartId");
    const ClangBackEnd::FileContainer fileContainer{filePath, projectPartId};
    const ClangBackEnd::FileContainer headerContainer{headerPath, projectPartId};
};

TEST_F(TranslationUnits, ThrowForGettingWithWrongFilePath)
{
    ASSERT_THROW(translationUnits.translationUnit(nonExistingFilePath, projectPartId),
                 ClangBackEnd::TranslationUnitDoesNotExistException);

}

TEST_F(TranslationUnits, ThrowForGettingWithWrongProjectPartFilePath)
{
    ASSERT_THROW(translationUnits.translationUnit(filePath, nonExistingProjectPartId),
                 ClangBackEnd::ProjectPartDoNotExistException);

}

TEST_F(TranslationUnits, ThrowForAddingNonExistingFile)
{
    ClangBackEnd::FileContainer fileContainer(nonExistingFilePath, projectPartId);

    ASSERT_THROW(translationUnits.create({fileContainer}),
                 ClangBackEnd::TranslationUnitFileNotExitsException);
}

TEST_F(TranslationUnits, DoNotThrowForAddingNonExistingFileWithUnsavedContent)
{
    ClangBackEnd::FileContainer fileContainer(nonExistingFilePath, projectPartId, Utf8String(), true);

    ASSERT_NO_THROW(translationUnits.create({fileContainer}));
}

TEST_F(TranslationUnits, Add)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);

    translationUnits.create({fileContainer});

    ASSERT_THAT(translationUnits.translationUnit(filePath, projectPartId),
                IsTranslationUnit(filePath, projectPartId, 74u));
}

TEST_F(TranslationUnits, AddAndTestCreatedTranslationUnit)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);

    auto createdTranslationUnits = translationUnits.create({fileContainer});

    ASSERT_THAT(createdTranslationUnits.front(),
                IsTranslationUnit(filePath, projectPartId, 74u));
}

TEST_F(TranslationUnits, ThrowForCreatingAnExistingTranslationUnit)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);
    translationUnits.create({fileContainer});

    ASSERT_THROW(translationUnits.create({fileContainer}),
                 ClangBackEnd::TranslationUnitAlreadyExistsException);
}

TEST_F(TranslationUnits, ThrowForUpdatingANonExistingTranslationUnit)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);
    ASSERT_THROW(translationUnits.update({fileContainer}),
                 ClangBackEnd::TranslationUnitDoesNotExistException);
}

TEST_F(TranslationUnits, UpdateSingle)
{
    ClangBackEnd::FileContainer createFileContainer(filePath, projectPartId, Utf8StringVector(), 74u);
    ClangBackEnd::FileContainer updateFileContainer(filePath, Utf8String(), Utf8StringVector(), 75u);
    translationUnits.create({createFileContainer});

    translationUnits.update({updateFileContainer});

    ASSERT_THAT(translationUnits.translationUnit(filePath, projectPartId),
                IsTranslationUnit(filePath, projectPartId, 75u));
}

TEST_F(TranslationUnits, UpdateMultiple)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);
    ClangBackEnd::FileContainer fileContainerWithOtherProject(filePath, otherProjectPartId, Utf8StringVector(), 74u);
    ClangBackEnd::FileContainer updatedFileContainer(filePath, Utf8String(), Utf8StringVector(), 75u);
    translationUnits.create({fileContainer, fileContainerWithOtherProject});

    translationUnits.update({updatedFileContainer});

    ASSERT_THAT(translationUnits.translationUnit(filePath, projectPartId),
                IsTranslationUnit(filePath, projectPartId, 75u));
    ASSERT_THAT(translationUnits.translationUnit(filePath, otherProjectPartId),
                IsTranslationUnit(filePath, otherProjectPartId, 75u));
}

TEST_F(TranslationUnits, UpdateUnsavedFileAndCheckForReparse)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);
    ClangBackEnd::FileContainer headerContainer(headerPath, projectPartId, Utf8StringVector(), 74u);
    ClangBackEnd::FileContainer headerContainerWithUnsavedContent(headerPath, projectPartId, Utf8String(), true, 75u);
    translationUnits.create({fileContainer, headerContainer});
    TranslationUnit translationUnit = translationUnits.translationUnit(filePath, projectPartId);
    translationUnit.parse();

    translationUnits.update({headerContainerWithUnsavedContent});

    ASSERT_TRUE(translationUnits.translationUnit(filePath, projectPartId).isNeedingReparse());
}

TEST_F(TranslationUnits, RemoveFileAndCheckForReparse)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);
    ClangBackEnd::FileContainer headerContainer(headerPath, projectPartId, Utf8StringVector(), 74u);
    ClangBackEnd::FileContainer headerContainerWithUnsavedContent(headerPath, projectPartId, Utf8String(), true, 75u);
    translationUnits.create({fileContainer, headerContainer});
    TranslationUnit translationUnit = translationUnits.translationUnit(filePath, projectPartId);
    translationUnit.parse();

    translationUnits.remove({headerContainerWithUnsavedContent});

    ASSERT_TRUE(translationUnits.translationUnit(filePath, projectPartId).isNeedingReparse());
}

TEST_F(TranslationUnits, DontGetNewerFileContainerIfRevisionIsTheSame)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);
    translationUnits.create({fileContainer});

    auto newerFileContainers = translationUnits.newerFileContainers({fileContainer});

    ASSERT_THAT(newerFileContainers.size(), 0);
}

TEST_F(TranslationUnits, GetNewerFileContainerIfRevisionIsDifferent)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId, Utf8StringVector(), 74u);
    ClangBackEnd::FileContainer newerContainer(filePath, projectPartId, Utf8StringVector(), 75u);
    translationUnits.create({fileContainer});

    auto newerFileContainers = translationUnits.newerFileContainers({newerContainer});

    ASSERT_THAT(newerFileContainers.size(), 1);
}

TEST_F(TranslationUnits, ThrowForRemovingWithWrongFilePath)
{
    ClangBackEnd::FileContainer fileContainer(nonExistingFilePath, projectPartId);

    ASSERT_THROW(translationUnits.remove({fileContainer}),
                 ClangBackEnd::TranslationUnitDoesNotExistException);
}

TEST_F(TranslationUnits, ThrowForRemovingWithWrongProjectPartFilePath)
{
    ClangBackEnd::FileContainer fileContainer(filePath, nonExistingProjectPartId);

    ASSERT_THROW(translationUnits.remove({fileContainer}),
                 ClangBackEnd::ProjectPartDoNotExistException);
}

TEST_F(TranslationUnits, Remove)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId);
    translationUnits.create({fileContainer});

    translationUnits.remove({fileContainer});

    ASSERT_THROW(translationUnits.translationUnit(filePath, projectPartId),
                 ClangBackEnd::TranslationUnitDoesNotExistException);
}

TEST_F(TranslationUnits, RemoveAllValidIfExceptionIsThrown)
{
    ClangBackEnd::FileContainer fileContainer(filePath, projectPartId);
    translationUnits.create({fileContainer});

    ASSERT_THROW(translationUnits.remove({ClangBackEnd::FileContainer(Utf8StringLiteral("dontextist.pro"), projectPartId), fileContainer}),
                 ClangBackEnd::TranslationUnitDoesNotExistException);

    ASSERT_THAT(translationUnits.translationUnits(),
                Not(Contains(TranslationUnit(filePath,
                                             projects.project(projectPartId),
                                             Utf8StringVector(),
                                             translationUnits))));
}

TEST_F(TranslationUnits, HasTranslationUnit)
{
    translationUnits.create({{filePath, projectPartId}});

    ASSERT_TRUE(translationUnits.hasTranslationUnit(filePath, projectPartId));
}

TEST_F(TranslationUnits, HasNotTranslationUnit)
{
    ASSERT_FALSE(translationUnits.hasTranslationUnit(filePath, projectPartId));
}

TEST_F(TranslationUnits, isUsedByCurrentEditor)
{
    translationUnits.create({fileContainer});
    auto translationUnit = translationUnits.translationUnit(fileContainer);

    translationUnits.setUsedByCurrentEditor(filePath);

    ASSERT_TRUE(translationUnit.isUsedByCurrentEditor());
}

TEST_F(TranslationUnits, IsNotCurrentEditor)
{
    translationUnits.create({fileContainer});
    auto translationUnit = translationUnits.translationUnit(fileContainer);

    translationUnits.setUsedByCurrentEditor(headerPath);

    ASSERT_FALSE(translationUnit.isUsedByCurrentEditor());
}

TEST_F(TranslationUnits, IsNotCurrentEditorAfterBeingCurrent)
{
    translationUnits.create({fileContainer});
    auto translationUnit = translationUnits.translationUnit(fileContainer);
    translationUnits.setUsedByCurrentEditor(filePath);

    translationUnits.setUsedByCurrentEditor(headerPath);

    ASSERT_FALSE(translationUnit.isUsedByCurrentEditor());
}

TEST_F(TranslationUnits, IsVisibleEditor)
{
    translationUnits.create({fileContainer});
    auto translationUnit = translationUnits.translationUnit(fileContainer);

    translationUnits.setVisibleInEditors({filePath});

    ASSERT_TRUE(translationUnit.isVisibleInEditor());
}

TEST_F(TranslationUnits, IsNotVisibleEditor)
{
    translationUnits.create({fileContainer});
    auto translationUnit = translationUnits.translationUnit(fileContainer);

    translationUnits.setVisibleInEditors({headerPath});

    ASSERT_FALSE(translationUnit.isVisibleInEditor());
}

TEST_F(TranslationUnits, IsNotVisibleEditorAfterBeingVisible)
{
    translationUnits.create({fileContainer});
    auto translationUnit = translationUnits.translationUnit(fileContainer);
    translationUnits.setVisibleInEditors({filePath});

    translationUnits.setVisibleInEditors({headerPath});

    ASSERT_FALSE(translationUnit.isVisibleInEditor());
}

void TranslationUnits::SetUp()
{
    projects.createOrUpdate({ProjectPartContainer(projectPartId)});
    projects.createOrUpdate({ProjectPartContainer(otherProjectPartId)});
}

}
