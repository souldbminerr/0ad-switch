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

// This pipeline builds the game on macOS (which uses the clang-10 compiler) and runs tests.

pipeline {
    // Stop previous build in pull requests, but not in branches
    options { disableConcurrentBuilds(abortPrevious: env.CHANGE_ID != null) }

    parameters {
        booleanParam description: 'Non-incremental build', name: 'CLEANBUILD'
    }

    agent none

    stages {
        stage('macOS builds') {
            matrix {
                axes {
                    axis {
                        name 'BUILD_TYPE'
                        values 'debug', 'release'
                    }
                }

                stages {
                    stage('Run') {
                        agent {
                            node {
                                label 'macOSAgentVentura'
                                customWorkspace 'workspace/clang13'
                            }
                        }

                        stages {
                            stage('Pre-build') {
                                steps {
                                    sh 'git lfs pull -I binaries/data/tests'
                                    sh 'git lfs pull -I "binaries/data/mods/_test.*"'

                                    sh "libraries/build-macos-libs.sh ${JOBS} 2> macos-prebuild-errors.log"
                                    sh 'build/workspaces/update-workspaces.sh 2>> macos-prebuild-errors.log'

                                    script {
                                        if (params.CLEANBUILD) {
                                            sh 'make -C build/workspaces/gcc config=${BUILD_TYPE} clean'
                                        }
                                    }
                                }
                                post {
                                    failure {
                                        echo(message: readFile(file: 'macos-prebuild-errors.log'))
                                    }
                                }
                            }

                            stage('Build') {
                                steps {
                                    sh '''
                                        rm -f thepipe
                                        mkfifo thepipe
                                        tee build.log < thepipe &
                                        make -C build/workspaces/gcc/ ${JOBS} config=${BUILD_TYPE} > thepipe 2>&1
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
                                                    name: env.BUILD_TYPE,
                                                    id : env.BUILD_TYPE,
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

                            stage('Tests') {
                                steps {
                                    timeout(time: 15) {
                                        script {
                                            def bin = env.BUILD_TYPE == 'debug' ? 'test_dbg' : 'test'
                                            sh "./binaries/system/${bin} --format junit --output cxxtest.xml"
                                        }
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
                }
            }
        }
    }
}
