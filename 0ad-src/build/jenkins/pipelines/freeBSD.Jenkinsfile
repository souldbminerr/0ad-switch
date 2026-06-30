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

// This pipeline builds the game on FreeBSD (which uses the clang-17 compiler) and runs tests.
// Due to a compilation bug with SpiderMonkey, the game is only built with the release configuration.

pipeline {
    // Stop previous build in pull requests, but not in branches
    options { disableConcurrentBuilds(abortPrevious: env.CHANGE_ID != null) }

    parameters {
        booleanParam description: 'Non-incremental build', name: 'CLEANBUILD'
    }

    agent {
        node {
            label 'FreeBSDAgent'
            customWorkspace 'workspace/clang17'
        }
    }

    environment {
        USE = 'iconv'
        WX_CONFIG = '/usr/local/bin/wxgtk3u-3.2-config'
        CXXFLAGS = '-stdlib=libc++ '
        LLVM_OBJDUMP = '/usr/bin/llvm-objdump'
    }

    stages {
        stage('Pre-build') {
            steps {
                sh 'git lfs pull -I binaries/data/tests'
                sh 'git lfs pull -I "binaries/data/mods/_test.*"'

                sh "libraries/build-source-libs.sh ${JOBS} 2> freebsd-prebuild-errors.log"
                sh 'build/workspaces/update-workspaces.sh 2>> freebsd-prebuild-errors.log'

                script {
                    if (params.CLEANBUILD) {
                        sh 'cd build/workspaces/gcc/ && gmake clean config=release'
                    }
                }
            }
            post {
                failure {
                    echo(message: readFile(file: 'freebsd-prebuild-errors.log'))
                }
            }
        }

        stage('Release Build') {
            steps {
                sh '''
                    rm -f thepipe
                    mkfifo thepipe
                    tee build.log < thepipe &
                    gmake -C build/workspaces/gcc/ ${JOBS} config=release > thepipe 2>&1
                    status=$?
                    rm thepipe
                    exit ${status}
                '''
            }
            post {
                failure {
                    script {
                        if (!params.CLEANBUILD) {
                            build wait: false, job: "$JOB_NAME", parameters: [booleanParam(name: 'CLEANBUILD', value: true)]
                        }
                    }
                }
                always {
                    script {
                        recordIssues(
                            tool: analysisParser(
                                analysisModelId: 'clang',
                                name: 'Release Build',
                                id : 'release',
                                pattern: 'build.log'
                            ),
                            skipPublishingChecks: true,
                            enabledForFailure: true,
                            qualityGates: [[threshold: 1, type: 'TOTAL', criticality: 'FAILURE']]
                        )
                    }
                }
            }
        }

        stage('Release Tests') {
            steps {
                timeout(time: 15) {
                    sh './binaries/system/test --format junit --output cxxtest.xml'
                }
            }
            post {
                always {
                    junit(skipPublishingChecks: true, testResults: 'cxxtest.xml')
                }
            }
        }
    }
}
