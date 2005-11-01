<xsl:stylesheet version="1.0" 
		xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<!-- poldek.conf.xml -> tests/poldek_test_conf.conf -->
<!-- $Id$ -->

<xsl:output method="text" indent="no" />
<xsl:template match="/">
  <xsl:for-each select="config/confsection">
[<xsl:value-of select="@name"/>]
  <xsl:for-each select="optiongroup">
        <xsl:for-each select="option[@type='boolean' or @type='boolean3']">
          <xsl:choose>
            <xsl:when test="@default='yes'">
<xsl:value-of select="@name"/> = no
            </xsl:when>
            <xsl:when test="@default='no'">
<xsl:value-of select="@name"/> = yes
            </xsl:when>
            <xsl:when test="@default='auto'">
<xsl:value-of select="@name"/> = auto
            </xsl:when>
            <xsl:otherwise>
<xsl:value-of select="@name"/> = yes
            </xsl:otherwise>
          </xsl:choose>
        </xsl:for-each>

        <xsl:for-each select="option[@type='string']">
          <xsl:if test="not(contains(@name, '*'))">
          <xsl:choose>
            <xsl:when test="string-length(@default) > 0">
              <xsl:value-of select="@name"/> = <xsl:value-of select="@default"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:value-of select="@name"/> = <xsl:value-of select="translate(@name,' ','_')"/>
            </xsl:otherwise>
          </xsl:choose>
            #
          </xsl:if>
        </xsl:for-each>

        <xsl:for-each select="option[@type='integer']">
          <xsl:choose>
            <xsl:when test="string-length(@default) > 0">
              <xsl:value-of select="@name"/> = <xsl:value-of select="@default"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:value-of select="@name"/> = 100;
            </xsl:otherwise>
          </xsl:choose>
            #
        </xsl:for-each>
        

      </xsl:for-each>
    </xsl:for-each>
</xsl:template>
</xsl:stylesheet>

