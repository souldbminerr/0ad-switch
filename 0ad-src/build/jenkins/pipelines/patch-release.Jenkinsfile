/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

// This pipeline is used to generate patch releases.
// It starts by generating a patch file in a dedicated workspace, then it triggers a downstream job
// which is an instance of the bundles pipeline, passing the patch file to it.

pipeline {
    agent {
        node {
            label 'WindowsAgent'
        }
    }

    parameters {
        string(name: 'BUNDLE_VERSION', description: 'Bundle Version')
        string(name: 'NIGHTLY_REVISION', description: 'Revision of the nightly build corresponding to the release')
        string(name: 'RELEASE_TAG', description: 'Git tag from which the point release originates')
    }

    stages {
        stage('Generate Patch') {
            steps {
                checkout scmGit(branches: [[name: "${GIT_BRANCH}"]], extensions: [cleanAfterCheckout(), localBranch()])
                bat 'cd build\\build_version && build_version.bat'
                archiveArtifacts artifacts: 'build/build_version/build_version.txt'
                bat "git diff ${RELEASE_TAG}..HEAD > ${BUNDLE_VERSION}.patch"
                archiveArtifacts artifacts: "${params.BUNDLE_VERSION}.patch"
            }
        }

        stage('Bundle Patched Release') {
            steps {
                build job: '0ad-patch-bundles', wait: false, waitForStart: true, parameters: [
                    string(name: 'BUNDLE_VERSION', value: "${params.BUNDLE_VERSION}"),
                    string(name: 'NIGHTLY_REVISION', value: "${params.NIGHTLY_REVISION}"),
                    booleanParam(name: 'PATCH_BUILD', value: true)
                ]
            }
        }
    }
}
