#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "mweventhandler.h"
#include <QInputDialog>
#include <QFont>
#include <QColor>
#include <QDir>
#include <QStyle>
#include <QStandardPaths>
#include <QMessageBox>
#include <QTimer>
#include <QStandardItemModel>
#include <QPlainTextEdit>
#include <QDebug>
#include <QVariant>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow), event_handler(new MWEventHandler(this)) {
    ui->setupUi(this);
    create_setting();
    history_list = new HistoryList(setting);
    ui->centralWidget->setLayout(ui->horizontalLayout);
    move(setting->value("window_pos").value<QPoint>());
    const QSize& window_size = setting->value("window_size").value<QSize>();
    resize(window_size);
    setWindowFlag(Qt::FramelessWindowHint, setting->value("frameless").toBool());
    ui->listWidget->setHidden(!setting->value("sidebar").toBool());
    splitter = new QSplitter;
    ui->horizontalLayout->addWidget(splitter);
    splitter->addWidget(ui->listWidget);
    splitter->addWidget(ui->textEdit);
    QVariant splitter_state = setting->value("splitter_state");
    if (!splitter_state.isNull())
        splitter->restoreState(splitter_state.value<QByteArray>());
    update_style();
    ui->textEdit->setFocus();
    timer = new QTimer(this);
    timer->setInterval(setting->value("autosave_interval").toInt());
    timer->callOnTimeout(this, &MainWindow::auto_save);
}

MainWindow::~MainWindow() {
    close_current();
    setting->setValue("window_pos", QVariant::fromValue(pos()));
    setting->setValue("window_size", QVariant::fromValue(size()));
    setting->setValue("frameless", windowFlags().testFlag(Qt::FramelessWindowHint));
    setting->setValue("sidebar", !ui->listWidget->isHidden());
    setting->setValue("splitter_state", QVariant::fromValue(splitter->saveState()));
    setting->setValue("autosave_interval", QVariant::fromValue(timer->interval()));
    delete history_list;
    delete ui;
    delete event_handler;
}

void MainWindow::receive_args(int argc, char* argv[]) {
    if (QFile::exists(autosave_filepath) &&
        QMessageBox::question(
            this,
            "Retrieve",
            "Your document was not saved before an unexpected shutdown.\n"
            "Retrieve your document?\n"
            "Note: if you are retrieving a newly created file, enter an empty password in the next step."
        ) == QMessageBox::Yes) {
        set_filename("");
        display(autosave_filepath, false);
        set_dirty(true);
        return;
    }
    if (argc == 2) { // explicitly open file
        display(argv[1]);
        return;
    }
    if (history_list->repr().size()) { // not empty history
        display(history_list->get_latest().file);
        return;
    }
    on_actionNew_triggered();
}

void MainWindow::post_show() {
    auto _font = setting->value("font").value<QFont>();
    ui->textEdit->setFont(_font);
    ui->listWidget->setFont(QFont(contents_fontname, contents_fontsize));
}

void MainWindow::keyPressEvent(QKeyEvent* keyEvent) {
    event_handler->key_press_handler(keyEvent);
}

void MainWindow::keyReleaseEvent(QKeyEvent* keyEvent) {
    event_handler->key_release_handler(keyEvent);
}

void MainWindow::display(QString filepath, bool updateFilename) {
    if (!QFile::exists(filepath)) {
        on_actionNew_triggered();
        return;
    }
    bool ok; QString password;
    password = QInputDialog::getText(this, "password", QString("Password for document %1 (leave it blank if this is a plain text document):").arg(filepath), QLineEdit::Password, "", &ok);
    if (ok) {
        if (updateFilename)
            set_filename(filepath);
        std_file.file_path = filepath;
        uint64_t key = password.toULong();
        autosave_file.update_key(key);
        std_file.update_key(key);
        QString text = std_file.read();
        ui->textEdit->setPlainText(text);
        int cursor_pos = history_list->get_entry(filepath).cursor;
        if (cursor_pos)
            set_cursor_pos(cursor_pos);
        update_index(text);
        text_connection = connect(ui->textEdit, &QPlainTextEdit::textChanged, this, &MainWindow::on_text_modified);
        timer->start();
    }
    else    on_actionNew_triggered();
}

void MainWindow::on_actionNew_triggered() {
    close_current();
    set_filename("");
    const QString& history_repr = history_list->repr();
    if (history_repr.isEmpty())
        ui->textEdit->setPlainText(QString(additional_welcome_message) + help_text_full + dismiss_reminder);
    else
        ui->textEdit->setPlainText(QString(history_prompt) + history_repr + help_text_short + dismiss_reminder);
    ui->textEdit->setReadOnly(true); // TODO: unable to reset after pressing ctrl-H
}

bool MainWindow::on_actionOpen_triggered() {
    QString file = QFileDialog::getOpenFileName(this, "Open a file", "", "Text files(*.enc *.txt)");
    if (!file.isEmpty()) {
        close_current();
        display(file);
        return true;
    }
    return false;
}

void MainWindow::on_actionSave_triggered() {
    save_current();
}

void MainWindow::on_actionSave_As_triggered() {
    QString file = QFileDialog::getSaveFileName(this, "Save as", "my.enc", "Text files(*.enc *.txt)");
    if (file.endsWith(autosave_filepath)) {
        QMessageBox::warning(this, "Invalid filepath", "File ending with '.autosave' is not allowed!");
        on_actionSave_As_triggered();
        return;
    }
    if (!file.isEmpty()) {
        set_filename(file);
        if (file.endsWith(".txt")) {
            autosave_file.update_key(0);
            std_file.update_key(0);
        }
        else {
            QString confirm_password, password;
            while (true) {
                bool ok;
                password = QInputDialog::getText(this, "password", QString("A digit password to encript document %1 (leave it blank if you don't want encryption):").arg(file), QLineEdit::Password, "", &ok);
                if (!ok)    return;
                if (password.isEmpty())
                    break;
                confirm_password = QInputDialog::getText(this, "password", "Please confirm your password:", QLineEdit::Password, "", &ok);
                if (!ok)    return;
                if (password != confirm_password)
                    QMessageBox::warning(this, "password", "Password mismatch!");
                else
                    break;
            }
            uint64_t key = password.toULong();
            autosave_file.update_key(key);
            std_file.update_key(key);
        }
        QFile _file(file);
        _file.open(QIODevice::WriteOnly);
        _file.close();
        save_current(true);
    }
}

void MainWindow::on_actionCopy_triggered() {
    ui->textEdit->copy();
}

void MainWindow::on_actionPaste_triggered() {
    ui->textEdit->paste();
}

void MainWindow::on_actionCut_triggered() {
    ui->textEdit->cut();
}

void MainWindow::on_actionUndo_triggered() {
    ui->textEdit->undo();
}

void MainWindow::on_actionRedo_triggered() {
    ui->textEdit->redo();
}

void MainWindow::on_text_modified() {
    set_dirty(true);
}

void MainWindow::auto_save() {
    QString text = ui->textEdit->toPlainText();
    autosave_file.write(text);
}

void MainWindow::set_filename(QString filename) {
    std_file.file_path = filename;
    int i = filename.length();
    if (!i)   filename = "new*";
    while (i && filename[--i] != '/');
    setWindowTitle(filename.mid(i + (filename[i] == '/')) + " - EncEdit");
}

void MainWindow::set_cursor_pos(int pos) {
    auto cursor = ui->textEdit->textCursor();
    cursor.setPosition(pos);
    ui->textEdit->setTextCursor(cursor);
}

void MainWindow::set_dirty(bool val) {
    if (val == dirty)
        return;
    if (val) {
        dirty = true;
        QString title = windowTitle();
        if (title.back() != '*')
            setWindowTitle(title + "*");
    }
    else {
        dirty = false;
        QString title = windowTitle();
        if (title.back() == '*')
            setWindowTitle(title.mid(0, title.length() - 1));
    }
}

void MainWindow::close_current() {
    disconnect(text_connection);
    timer->stop();
    if (dirty && !ui->textEdit->toPlainText().isEmpty()) {
        auto response = QMessageBox::question(this, "Save", "Save your document?");
        if (response == QMessageBox::Yes)
            save_current();
    }
    set_dirty(false);
    if (std_file.file_path.size())
        history_list->update_now(std_file.file_path, ui->textEdit->textCursor().position());
    QFile::remove(autosave_filepath);
}

void MainWindow::save_current(bool saveClean) {
    // TODO: immediately close after saving and the cursor position won't be saved
    if (!dirty && !saveClean)
        return;
    if (!QFile::exists(std_file.file_path) || std_file.file_path == autosave_filepath) {
        on_actionSave_As_triggered();
        return;
    }
    std_file.write(ui->textEdit->toPlainText(), saveClean);
    set_dirty(false);
    update_index(ui->textEdit->toPlainText());
}

void MainWindow::update_index(const QString& text, const QString& regexp) {
    index.load(text, regexp.size() ? regexp : setting->value("title_regexp").toString());
    ui->listWidget->clear();
    auto& strl = index.string_list();
    ui->listWidget->addItems(strl);
    connect(ui->listWidget, &QAbstractItemView::clicked, this, &MainWindow::on_listWidget_clicked);
}

void MainWindow::update_style() {
    QString style = QString("{background-color: rgb(%1); color: rgb(%2);}").arg(color2str(setting->value("background_color").toInt())).arg(color2str(setting->value("font_color").toInt()));
    ui->textEdit->setStyleSheet("QPlainTextEdit " + style);
    ui->listWidget->setStyleSheet("QListWidget " + style);
}

void MainWindow::create_setting() {
    QString appDataDirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    appDataDirPath = appDataDirPath.mid(0, appDataDirPath.lastIndexOf('/'));
    appDataDirPath = appDataDirPath.mid(0, appDataDirPath.lastIndexOf('/'));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, appDataDirPath);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    setting = new QSettings(this);
    for (const auto& conf : default_config)
        if (!setting->contains(conf.first))
            setting->setValue(conf.first, conf.second);
}

void MainWindow::on_listWidget_clicked(const QModelIndex& idx) {
    auto cursor = ui->textEdit->textCursor();
    cursor.setPosition(ui->textEdit->toPlainText().size());
    ui->textEdit->setTextCursor(cursor);
    cursor.setPosition(index[idx]);
    ui->textEdit->setTextCursor(cursor);
}
