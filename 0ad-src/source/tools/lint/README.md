# Linters

Linters for use in CI or by developers. Also providing configurations for IDEs.

## cppcheck

### suppression-list

The suppression list is ideally empty, restricting to file scope is preferred.

The format for an error suppression is one of:
[error id]:[filename]:[line]
[error id]:[filename2]
[error id]

### libraries

Adding library cfg's for other deps could improve cppchecks ability to find issues.

## copyright

A linter for checking copyright dates in file headers are up to date.

## eslint

For eslint run 'pre-commt run eslint -a'

### Installation and IDE integration

Install Node.js and then run 'npm install' in the repo root.

Now you can run eslint as 'npm run-script lint' or if you want eslint to try
fix the issues 'npm run-script lint:fix'.

After having installed eslint you might want to add an eslint extension to your
editor to get inline warnings and to allow for auto-formatting.
