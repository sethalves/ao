#include <QApplication>

#include "gui/interpreter.hpp"
#include "ao-guile.h"

_Interpreter::_Interpreter()
{
    connect(&thread, &QThread::started, this, &_Interpreter::init);
    moveToThread(&thread);
}

static void init_interpreter(void*)
{
    QFile i(":/scm/interpreter.scm");
    i.open(QIODevice::ReadOnly);
    scm_c_eval_string(i.readAll().constData());
}

void _Interpreter::init()
{
    // Modify environmental variables to use local Guile path
    auto path = QCoreApplication::applicationDirPath().toLocal8Bit() +
                "/../Resources/guile/";
    qputenv("GUILE_LOAD_COMPILED_PATH", path + "ccache/");
    qputenv("GUILE_LOAD_PATH", path + "scm/");

    scm_init_guile();

    scm_init_ao_modules();
    scm_c_use_module("ao kernel");

    // Initialize the interpreter module, which includes a variety of
    // helpful functions (including eval-sandboxed)
    scm_c_define_module("interpreter", init_interpreter, NULL);

    scm_eval_sandboxed = scm_c_eval_string(R"(
(use-modules (interpreter))
eval-sandboxed
)");

    scm_port_eof_p = scm_c_eval_string(R"(
(use-modules (rnrs io ports))
port-eof?
)");
    scm_syntax_error_sym = scm_from_utf8_symbol("syntax-error");
    scm_numerical_overflow_sym = scm_from_utf8_symbol("numerical-overflow");
    scm_valid_sym = scm_from_utf8_symbol("valid");
    scm_result_fmt = scm_from_locale_string("~S");
    scm_other_error_fmt = scm_from_locale_string("~A: ~A");
    scm_syntax_error_fmt = scm_from_locale_string("~A: ~A in form ~A");
    scm_numerical_overflow_fmt = scm_from_locale_string("~A: ~A in ~A");

    // Protect all of our interpreter vars from garbage collection
    for (auto s : {scm_eval_sandboxed, scm_port_eof_p, scm_valid_sym,
                   scm_syntax_error_sym, scm_numerical_overflow_sym,
                   scm_result_fmt, scm_syntax_error_fmt,
                   scm_numerical_overflow_fmt, scm_other_error_fmt})
    {
        scm_permanent_object(s);
    }

    auto kws = scm_to_locale_string(scm_c_eval_string(R"(
(string-drop (string-drop-right
    (format #f "~A" (apply append (map cdr sandbox-bindings))) 1) 1)
)"));
    emit(keywords(kws));
    free(kws);
}

void _Interpreter::eval()
{
    // Safely read in the text of the script
    QString script;
    {
        QMutexLocker lock(&mutex);
        script = _script;
    }

    auto result = scm_call_1(scm_eval_sandboxed,
            scm_from_locale_string(script.toLocal8Bit().data()));

    //  Loop through the whole result list, looking for an invalid clause
    bool valid = true;
    for (auto r = result; !scm_is_null(r) && valid; r = scm_cdr(r))
    {
        if (!scm_is_eq(scm_caar(r), scm_valid_sym))
        {
            valid = false;
        }
    }

    // If there is at least one result, then we'll convert the last one
    // into a string (with special cases for various erorr forms)
    auto last = scm_is_null(result) ? nullptr
                                    : scm_cdr(scm_car(scm_last_pair(result)));
    if (!valid)
    {
        /* last = '(before after key params) */
        auto before = scm_car(last);
        auto after = scm_cadr(last);
        auto key = scm_caddr(last);
        auto params = scm_cadddr(last);

        auto _stack = scm_car(scm_cddddr(last));
        SCM _str = nullptr;

        if (scm_is_eq(key, scm_syntax_error_sym))
        {
            _str = scm_simple_format(SCM_BOOL_F, scm_syntax_error_fmt,
                   scm_list_3(key, scm_cadr(params), scm_cadddr(params)));
        }
        else if (scm_is_eq(key, scm_numerical_overflow_sym))
        {
            _str = scm_simple_format(SCM_BOOL_F, scm_numerical_overflow_fmt,
                   scm_list_3(key, scm_cadr(params), scm_car(params)));
        }
        else
        {
            _str = scm_simple_format(SCM_BOOL_F, scm_other_error_fmt,
                   scm_list_2(key, scm_simple_format(
                        SCM_BOOL_F, scm_cadr(params), scm_caddr(params))));
        }
        auto str = scm_to_locale_string(_str);
        auto stack = scm_to_locale_string(_stack);
        emit(gotError(QString(str), QString(stack),
                    {scm_to_int(scm_car(before)),
                     scm_to_int(scm_car(after)),
                     scm_to_int(scm_cdr(before)),
                     scm_to_int(scm_cdr(after))}));
        free(str);
        free(stack);
    }
    else if (last)
    {
        auto str = scm_to_locale_string(
                scm_simple_format(SCM_BOOL_F, scm_result_fmt,
                                  scm_list_1(last)));
        emit(gotResult(QString(str)));
        free(str);
    }
    else
    {
        emit(gotResult("#<eof>"));
    }

    // Then iterate over the results, picking out shapes
    if (valid)
    {
        QList<Shape*> shapes;
        std::shared_ptr<std::map<Kernel::Tree::Id, float>> vars;
        QMap<Kernel::Tree::Id, Editor::Range> var_pos;

        while (!scm_is_null(result))
        {
            if (scm_is_tree(scm_cdar(result)))
            {
                if (vars.get() == nullptr)
                {
                    vars.reset(new std::map<Kernel::Tree::Id, float>);

                    auto vs = scm_c_eval_string(R"(
                        (use-modules (interpreter))
                        (hash-map->list (lambda (k v) v) vars) )");

                    for (auto v = vs; !scm_is_null(v); v = scm_cdr(v))
                    {
                        auto data = scm_cdar(v);
                        auto id = static_cast<Kernel::Tree::Id>(
                                ao_tree_id(scm_to_tree(scm_car(data))));
                        auto value = scm_to_double(scm_cadr(data));
                        (*vars)[id] = value;

                        auto vp = scm_caddr(data);
                        var_pos[id] = {scm_to_int(scm_car(vp)), 0,
                                       scm_to_int(scm_cadr(vp)),
                                       scm_to_int(scm_caddr(vp))};
                    }
                }
                auto tree = scm_to_tree(scm_cdar(result));
                auto shape = new Shape(*tree, vars);
                shape->moveToThread(QApplication::instance()->thread());
                shapes.push_back(shape);
            }
            result = scm_cdr(result);
        }
        emit(gotShapes(shapes));
        emit(gotVars(var_pos));
    }
}


////////////////////////////////////////////////////////////////////////////////

Interpreter::Interpreter()
{
    // Start evaluation some amount of time after the most recent change
    // to the script (should feel responsive, but we don't need to re-evaluate
    // every single character change).
    eval_timer.setSingleShot(true);
    eval_timer.setInterval(250);
    connect(&eval_timer, &QTimer::timeout, &interpreter, &_Interpreter::eval);

    // After the interpreter is kicked off, then emit the busy signal after
    // a short delay.  This is cancelled by either of the interpreter's
    // "done" signals (gotResult and gotError), to avoid jittering the UI
    // during very short evaluations.
    busy_timer.setSingleShot(true);
    busy_timer.setInterval(100);
    connect(&eval_timer, &QTimer::timeout, &busy_timer,
            QOverload<>::of(&QTimer::start));
    connect(&busy_timer, &QTimer::timeout, this, &Interpreter::busy);
    connect(&interpreter, &_Interpreter::gotResult,
            &busy_timer, [&](QString){ busy_timer.stop(); });
    connect(&interpreter, &_Interpreter::gotError, &busy_timer,
            [&](QString, QString, Editor::Range){ busy_timer.stop(); });

    // Forward all signals from _Interpreter (running in its own thread)
    connect(&interpreter, &_Interpreter::gotResult,
            this, &Interpreter::gotResult);
    connect(&interpreter, &_Interpreter::gotError,
            this, &Interpreter::gotError);
    connect(&interpreter, &_Interpreter::keywords,
            this, &Interpreter::keywords);
    connect(&interpreter, &_Interpreter::gotShapes,
            this, &Interpreter::gotShapes);
    connect(&interpreter, &_Interpreter::gotVars,
            this, &Interpreter::gotVars);
}

void Interpreter::start()
{
    interpreter.thread.start();
}

void Interpreter::onScriptChanged(QString s)
{
    {   // Swap the script into the other thread
        QMutexLocker lock(&interpreter.mutex);
        interpreter._script = s;
    }
    eval_timer.start();
}
