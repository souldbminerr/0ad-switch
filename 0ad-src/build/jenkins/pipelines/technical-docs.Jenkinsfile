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

// This pipeline is used to build the documentation.

pipeline {
    agent {
        dockerfile {
            label 'LinuxAgent'
            customWorkspace 'workspace/technical-docs'
            dir 'build/jenkins/dockerfiles'
            filename 'docs-tools.Dockerfile'
            // Prevent Jenkins from running commands with the UID of the host's jenkins user
            // https://stackoverflow.com/a/42822143
            args '-u root'
        }
    }

    stages {
        stage('Pull documentation assets') {
            steps {
                sh 'git lfs pull -I docs/doxygen'
            }
        }

        stage('Engine docs') {
            steps {
                sh 'cd docs/doxygen/ && cmake -S . -B build-docs && cmake --build build-docs'
            }
        }

        stage('Entity docs') {
            steps {
                sh 'cd binaries/system/ && svn export --force https://svn.wildfiregames.com/nightly-build/trunk/binaries/system/entity.rng'
                sh 'cd source/tools/entdocs/ && ./build.sh'
                sh 'cd source/tools/entdocs/ && mv entity-docs.html nightly.html'
            }
        }

        stage('Template Analyzer') {
            steps {
                sh 'cd source/tools/templatesanalyzer/ && python3 unit_tables.py'
                sh 'mv source/tools/templatesanalyzer/unit_summary_table.html source/tools/templatesanalyzer/index.html'
            }
        }

        stage('Upload') {
            steps {
                sshPublisher alwaysPublishFromMaster: true, failOnError: true, publishers: [
                    sshPublisherDesc(configName: 'docs.wildfiregames.com', transfers: [
                        sshTransfer(sourceFiles: 'docs/doxygen/output/html/**', removePrefix: 'docs/doxygen/output/html/', remoteDirectory: 'pyrogenesis'),
                        sshTransfer(sourceFiles: 'source/tools/entdocs/nightly.html', removePrefix: 'source/tools/entdocs', remoteDirectory: 'entity-docs'),
                        sshTransfer(sourceFiles: 'source/tools/templatesanalyzer/index.html', removePrefix: 'source/tools/templatesanalyzer', remoteDirectory: 'templatesanalyzer'),
                    ]
                )]
            }
        }
    }
}
