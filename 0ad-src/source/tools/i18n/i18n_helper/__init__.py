import os


L10N_FOLDER_NAME = "l10n"
TRANSIFEX_CLIENT_FOLDER = ".tx"
L10N_TOOLS_DIRECTORY = os.path.dirname(os.path.realpath(__file__))
PROJECT_ROOT_DIRECTORY = os.path.abspath(
    os.path.join(L10N_TOOLS_DIRECTORY, os.pardir, os.pardir, os.pardir, os.pardir)
)
