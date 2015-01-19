/* fhrus2openmensa - Convert the FH-Mensa Rüsselsheim plan to OpenMensa Feed v2
 *
 * 2015-01-18, Georg Sauthoff <mail@georg.so>
 *
 * GPLv3+
 *
 * Example:
 *
 *     $ curl -o fhrus.html \
 *       'http://www.studentenwerkfrankfurt.de/index.php?id=585&no_cache=1&type=98'
 *     $ tidy -o fhrus.xml -bare -clean -indent --show-warnings no \
 *            --hide-comments yes -numeric \
 *            -q -asxml fhrus.html
 *     $ ./fhrus2openmensa fhrus.xml > fhrus_feed.xml
 *     $ xmllint -noout -schema open-mensa-v2.xsd fhrus_feed.xml
 *
 */

#include <libxml++/libxml++.h>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
//#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <iostream>
#include <string>
#include <locale>
#include <exception>
#include <stdexcept>
using namespace std;
using namespace xmlpp;
namespace mp = boost::multiprecision;


class Today {
  private:
    string year_;
  public:
    Today();
    const string &year() const;
    static const Today &instance();
};
Today::Today()
{
  auto t = boost::posix_time::second_clock::local_time();
  year_ = boost::lexical_cast<string>(t.date().year());
}
const string &Today::year() const { return year_; };
const Today &Today::instance()
{
  static Today t;
  return t;
}

static const Node::PrefixNsMap namespaces = {
  { "xhtml", "http://www.w3.org/1999/xhtml" }
};

unsigned days(const Node *node)
{
  double d = node->eval_to_number("count(//xhtml:table[@class='tx_hmwbsffmmenu_menu date'])",
      namespaces);
  return boost::lexical_cast<unsigned>(d);
}

static string date(const Node *node, unsigned dow)
{
  string expr((boost::format("//xhtml:table[@class='tx_hmwbsffmmenu_menu date'][%1%]"
          "/xhtml:tr/xhtml:td/xhtml:strong/text()") % dow).str());
  string s(node->eval_to_string(expr, namespaces));

  static const boost::regex re(R"(^[A-Za-z ,]+([0-9]{2})\.([0-9]{2})\.$)");
  boost::smatch m;
  if (boost::regex_match(s, m, re)) {
    return (boost::format("%1%-%2%-%3%") % Today::instance().year() % m[2] % m[1]).str();
  } else {
    throw runtime_error("Unexpected date string: " + s);
  }
}

static string normalize(const string &s)
{
  static const boost::regex re(R"([ \t\n]+)");
  return boost::regex_replace(s, re, " ");
}
static string normalize_price(const string &s)
{
  static const boost::regex re(R"(^([0-9]+),([0-9]+)[^0-9]+$)");
  boost::smatch m;
  if (boost::regex_match(s, m, re)) {
    return (boost::format("%1%.%2%") % m[1] % m[2]).str();
  } else {
    throw runtime_error("Unexpected price string: " + s);
  }
}

static NodeSet names(const Node *node, unsigned dow)
{
  string expr((boost::format("//xhtml:table[@class='tx_hmwbsffmmenu_menu date'][%1%]/"
          "xhtml:tr/xhtml:td/xhtml:div/xhtml:strong/text()") % dow).str());
  auto r = node->find(expr, namespaces);
  return std::move(r);
}
static NodeSet notes(const Node *node, unsigned dow)
{
  string expr((boost::format("//xhtml:table[@class='tx_hmwbsffmmenu_menu date'][%1%]/"
          "/xhtml:tr/xhtml:td/xhtml:div/xhtml:p/text()") % dow).str());
  auto r = node->find(expr, namespaces);
  return std::move(r);
}
static NodeSet prices(const Node *node, unsigned dow)
{
  string expr((boost::format("//xhtml:table[@class='tx_hmwbsffmmenu_menu date'][%1%]/"
          "/xhtml:tr/xhtml:td/xhtml:p[@class='price']/xhtml:strong/text()") % dow).str());
  auto r = node->find(expr, namespaces);
  return std::move(r);
}

static mp::cpp_dec_float_50 guest_price(const string &s)
{
  mp::cpp_dec_float_50 r(s);
  mp::cpp_dec_float_50 x("1.6");
  return r + x;
}

static void gen_dow(const Node *root, unsigned dow, ostream &o)
{
  string d(std::move(date(root, dow)));
  o << "    <day date='" << d << "'>\n";
  NodeSet n(std::move(names(root, dow)));
  NodeSet m(std::move(notes(root, dow)));
  NodeSet p(std::move(prices(root, dow)));
  auto i = n.begin();
  auto j = m.begin();
  auto k = p.begin();
  unsigned x = 1;
  for (; i != n.end() && j != m.end() && k != p.end(); ++i, ++j, ++k, ++x) {
    o << "      <category name='Essen " << x << "'>\n";
    o << "        <meal>\n";
    ContentNode *name = dynamic_cast<ContentNode*>(*i);
    o << "          <name>" << normalize(name->get_content()) << "</name>\n";
    ContentNode *note = dynamic_cast<ContentNode*>(*j);
    o << "          <note>" << normalize(note->get_content()) << "</note>\n";
    ContentNode *price = dynamic_cast<ContentNode*>(*k);
    string charge(std::move(normalize_price(price->get_content())));
    o << "          <price role='student'>" << charge << "</price>\n";
    o << "          <price role='employee'>" << guest_price(charge) << "</price>\n";
    o << "          <price role='other'>" << guest_price(charge) << "</price>\n";
    o << "        </meal>\n";
    o << "      </category>\n";
  }
  o << "    </day>\n";
}

static void generate_openmensa(const Node *root, ostream &o)
{
  o << R"(<?xml version="1.0" encoding="UTF-8"?>
<openmensa version="2.0"
           xmlns="http://openmensa.org/open-mensa-v2"
           xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
           xsi:schemaLocation="http://openmensa.org/open-mensa-v2 http://openmensa.org/open-mensa-v2.xsd">
  <canteen>
)";
  unsigned n = days(root);
  for (unsigned i = 1; i<=n; ++i)
    gen_dow(root, i, o);
  o << R"(
  </canteen>
</openmensa>
)";
}

int main(int argc, char **argv)
{
  //std::locale::global(std::locale(""));
  //std::locale::global(std::locale().combine<std::ctype<char> >(std::locale("")) );
  // use the users local (otherwise glib throws on utf8 chars != ascii
  // + don't use braindead numpunct settings, e.g. 1,024 instead of 1024 ...
  std::locale::global(std::locale("").combine<std::numpunct<char> >(std::locale()) );
  try {
    string filename(argv[1]);
    DomParser parser;
    parser.set_substitute_entities(true);
    parser.parse_file(filename);
    generate_openmensa(parser.get_document()->get_root_node(), cout);
  } catch (const std::exception &e) {
    cerr << "Fail: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
